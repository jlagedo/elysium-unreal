"""Story 5 step 5's move, binding and consumer records and their phase-5 source checks, on
synthetic trees plus the committed records."""
from __future__ import annotations

import collections
import csv
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research/tooling/ghidra/driver"))
sys.path.insert(0, str(REPO / "research/tooling"))

import kernel_migration as km  # noqa: E402
import kernel_migration_step5 as s5  # noqa: E402


def write(root: Path, relative: Path | str, text: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


COMPONENT = ("struct FElysiumNpcBaseMemory\n{\n\tFElysiumEntityHandle Enemy;\n"
             "\tFElysiumEntityHandle LastSeen[static_cast<int32>(ESeen::Count)];\n\tvoid Reset();\n};\n"
             "struct FElysiumNpcScheduleHost\n{\n\tdouble NextThink = 0.0;\n};\n")


def tree(root: Path, troika_body: str, base_body: str = "", base_cpp: str = "", family_inl: str = "",
         registry: str = "") -> Path:
    write(root, s5.NPC_HEADER, "class FElysiumNpc : public FElysiumNpcBase\n{\npublic:\n"
          f"{troika_body}\t#include \"Substrate/ElysiumNpcFamily.inl\"\n}};\n")
    write(root, s5.SUBSTRATE / "ElysiumNpcFamily.inl", family_inl)
    write(root, s5.BASE_HEADER, "class FElysiumNpcBase : public FElysiumScriptedCharacter, public IElysiumScheduleRunner\n"
          f"{{\npublic:\n{base_body}\t#include \"Substrate/ElysiumNpcBaseFamily.inl\"\n}};\n")
    write(root, s5.SUBSTRATE / "ElysiumNpcBaseFamily.inl", "")
    write(root, s5.SUBSTRATE / "ElysiumNpcBaseFamily.cpp", base_cpp)
    write(root, s5.SUBSTRATE / "ElysiumNpcComponents.h", COMPONENT)
    write(root, s5.REGISTRY, registry)
    return root


def row(**values) -> dict:
    base = {"member": "Helper", "kind": "method", "declared_in": "ElysiumNpcFamily.inl",
            "defined_in": "ElysiumNpcFamily.cpp", "addresses": "10273390", "anchor": "base",
            "final_owner": "FElysiumNpcBase", "disposition": "base", "packet": "5e", "note": "-"}
    return {**base, **values}


def test_class_members_follow_the_class_includes_and_record_field_types(tmp_path):
    root = tree(tmp_path, "\tvirtual void Think() override;\n\tFElysiumNpcScheduleHost ScheduleHost;\n",
                family_inl="void Helper(int32 A);\nconst TArray<int32>* Rows = nullptr;\n")
    members = s5.class_members(root, s5.NPC_HEADER, "FElysiumNpc")
    assert members["Think"]["virtual"] and members["Think"]["override"]
    assert members["ScheduleHost"]["type"] == "FElysiumNpcScheduleHost"
    assert members["Helper"]["file"] == "ElysiumNpcFamily.inl"
    assert members["Rows"]["kind"] == "field"


def test_struct_fields_see_array_members_and_their_types(tmp_path):
    root = tree(tmp_path, "")
    fields = s5.struct_fields(root, "FElysiumNpcBaseMemory")
    assert fields == {"Enemy": "FElysiumEntityHandle", "LastSeen": "FElysiumEntityHandle"}


def test_a_component_path_resolves_through_its_holder_and_structs(tmp_path):
    root = tree(tmp_path, "\tFElysiumNpcScheduleHost ScheduleHost;\n",
                base_body="\tFElysiumNpcBaseMemory BaseMemory;\n")
    holders = {"FElysiumNpc": s5.class_members(root, s5.NPC_HEADER, "FElysiumNpc"),
               "FElysiumNpcBase": s5.class_members(root, s5.BASE_HEADER, "FElysiumNpcBase")}
    assert s5.resolve_path(root, holders, "FElysiumNpcBase::BaseMemory.LastSeen")
    assert s5.resolve_path(root, holders, "FElysiumNpc::ScheduleHost.NextThink")
    assert not s5.resolve_path(root, holders, "FElysiumNpc::BaseMemory.Enemy")
    assert not s5.resolve_path(root, holders, "FElysiumNpcBase::BaseMemory.Reset")


def test_a_base_move_is_declared_and_defined_on_the_base_and_gone_from_the_troika(tmp_path):
    root = tree(tmp_path, "", base_body="\tvoid Helper();\n", base_cpp="void FElysiumNpcBase::Helper() { }\n")
    assert s5.check_moves([row()], root) == collections.Counter({"base": 1})
    stale = tree(tmp_path / "stale", "\tvoid Helper();\n", base_body="\tvoid Helper();\n",
                 base_cpp="void FElysiumNpcBase::Helper() { }\n")
    with pytest.raises(km.InvalidManifest, match="still declared on FElysiumNpc"):
        s5.check_moves([row()], stale)
    undefined = tree(tmp_path / "undefined", "", base_body="\tvoid Helper();\n")
    with pytest.raises(km.InvalidManifest, match="not defined"):
        s5.check_moves([row()], undefined)


def test_a_rename_lands_under_the_slot_name_and_a_pair_is_a_troika_override(tmp_path):
    root = tree(tmp_path, "\tvirtual bool ShouldPlayFloatSound() override;\n",
                base_body="\tvirtual bool ShouldPlayFloatSound();\n",
                base_cpp="bool FElysiumNpcBase::ShouldPlayFloatSound() { return false; }\n")
    rename = row(member="BaseShouldPlayFloatSound", disposition="rename:ShouldPlayFloatSound")
    pair = row(member="ShouldPlayFloatSound", final_owner="FElysiumNpc", disposition="pair:ShouldPlayFloatSound")
    assert s5.check_moves([rename, pair], root) == collections.Counter({"rename": 1, "pair": 1})
    not_override = tree(tmp_path / "plain", "\tbool ShouldPlayFloatSound();\n",
                        base_body="\tvirtual bool ShouldPlayFloatSound();\n",
                        base_cpp="bool FElysiumNpcBase::ShouldPlayFloatSound() { return false; }\n")
    with pytest.raises(km.InvalidManifest, match="not an override"):
        s5.check_moves([pair], not_override)


def test_a_split_component_word_leaves_its_old_path(tmp_path):
    split = row(member="Senses.Memory.Enemy", kind="component-field", defined_in="-", addresses="+0x5ce0",
                final_owner="FElysiumNpcBase::BaseMemory.Enemy", disposition="split:BaseMemory", packet="5d")
    root = tree(tmp_path, "", base_body="\tFElysiumNpcBaseMemory BaseMemory;\n")
    assert s5.check_moves([split], root) == collections.Counter({"split": 1})
    missing = tree(tmp_path / "missing", "")
    with pytest.raises(km.InvalidManifest, match="does not resolve"):
        s5.check_moves([split], missing)


def test_a_staying_member_must_stay_and_investigate_refuses(tmp_path):
    root = tree(tmp_path, "\tvoid Stays();\n")
    stay = row(member="Stays", final_owner="FElysiumNpc", disposition="stay:troika body", packet="-")
    assert s5.check_moves([stay], root) == collections.Counter({"stay": 1})
    with pytest.raises(km.InvalidManifest, match="left FElysiumNpc"):
        s5.check_moves([stay], tree(tmp_path / "gone", ""))
    with pytest.raises(km.InvalidManifest, match="investigation"):
        s5.check_moves([row(disposition="investigate")], root)


def test_the_registry_chain_is_base_then_troika(tmp_path):
    good = ('Reg.RegisterAbstract("CAI_BaseNPC", ElysiumCombatCharacterClassName());\n'
            'Reg.RegisterAbstract("CAI_BaseNPCTroika", "CAI_BaseNPC");\n')
    s5.check_registry(tree(tmp_path, "", registry=good))
    with pytest.raises(km.InvalidManifest, match="under CAI_BaseNPC"):
        s5.check_registry(tree(tmp_path / "flat", "", registry=(
            'Reg.RegisterAbstract("CAI_BaseNPC", ElysiumCombatCharacterClassName());\n'
            'Reg.RegisterAbstract("CAI_BaseNPCTroika", ElysiumCombatCharacterClassName());\n')))


def test_a_base_binding_is_generated_on_the_base_only(tmp_path):
    root = tree(tmp_path, "", base_body="\tint32 CollisionMask = 0;\n")
    field = {"declaring_class": "CAI_BaseNPC", "offset": "0x1a44", "member": "m_iCollisionMask", "type": "integer",
             "flags": "SAVE", "external": "-", "kind": "field", "current": "saved",
             "current_path": "FElysiumNpc::CollisionMask", "final_owner": "FElysiumNpcBase",
             "final_path": "FElysiumNpcBase::CollisionMask", "disposition": "save", "packet": "5d", "note": ""}
    base_fn = ('\n\tvoid AddNpcBaseSaveFields(FElysiumClassDesc& D)\n\t{\n\t\tX(TEXT("m_iCollisionMask"));\n\t}\n')
    write(root, s5.BINDINGS, base_fn)
    assert s5.check_fields([field], root) == collections.Counter({"save": 1})
    troika_fn = ('\n\tvoid AddNpcSaveFields(FElysiumClassDesc& D)\n\t{\n\t\tX(TEXT("m_iCollisionMask"));\n\t}\n')
    write(root, s5.BINDINGS, base_fn + troika_fn)
    with pytest.raises(km.InvalidManifest, match="still generated on the Troika"):
        s5.check_fields([field], root)
    write(root, s5.BINDINGS, base_fn)
    with pytest.raises(km.InvalidManifest, match="no storage"):
        s5.check_fields([{**field, "final_path": "FElysiumNpcBase::Missing"}], root)


def test_read_moves_refuses_a_base_move_to_another_owner(tmp_path):
    path = tmp_path / "moves.tsv"
    with path.open("w", encoding="utf-8", newline="\n") as out:
        writer = csv.DictWriter(out, s5.MOVE_COLUMNS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerow(row(final_owner="FElysiumNpc"))
    with pytest.raises(km.InvalidManifest, match="names FElysiumNpcBase"):
        s5.read_moves(path)


def test_the_ledger_reads_a_layer_from_the_namespace_or_both_neighbours(tmp_path):
    functions = ("| Address | Function |\n"
                 "| `0x10273000` | CAI_BaseNPC::A | 1 |\n| `0x10273100` | FUN_10273100 | 1 |\n"
                 "| `0x10273200` | CAI_BaseNPC::B | 1 |\n| `0x10299000` | FUN_10299000 | 1 |\n"
                 "| `0x10299100` | CAI_BaseNPCTroika::C | 1 |\n| `0x10380000` | CNPC_VZombie::D | 1 |\n")
    write(tmp_path, s5.LEDGER / "functions.md", functions)
    write(tmp_path, s5.LEDGER / "slots.md", "")
    write(tmp_path, s5.LEDGER / "layout.tsv", "table\toffset\tsize\tmember\ttype\tfield_type\tcount\tflags\tlayer\n"
          "CAI_BaseNPCTroika\t0x1a44\t\tm_iCollisionMask\tint\t\t1\t\tCAI_BaseNPC\n")
    ledger = s5.Ledger(tmp_path)
    assert ledger.code_layer("10273000") == "base"
    assert ledger.code_layer("10273100") == "base"        # both neighbours say CAI_BaseNPC
    assert ledger.code_layer("10299000") == "unknown"     # base below, Troika above
    assert ledger.code_layer("10299100") == "troika"
    assert ledger.code_layer("10380000") == "species"
    assert ledger.word_layer(0x1a44) == "base"


def test_the_committed_records_are_complete_and_reviewed():
    manifest, _, _ = km.load()
    assert manifest["history"]["step4"]["commit"]
    moves = s5.read_moves(REPO / km.STORY / "moves-step5.tsv")
    fields = s5.read_fields(REPO / km.STORY / "fields-step5.tsv")
    consumers = s5.read_consumers(REPO / km.STORY / "consumers-step5.tsv")
    assert not [r for r in moves if r["disposition"] == "investigate"]
    assert not [r for r in consumers if r["disposition"] == "investigate"]
    assert len(fields) == 98 and {r["declaring_class"] for r in fields} == {"CAI_BaseNPC"}
    # Every bound or saved base word has base storage in the record.
    assert all(r["final_owner"] == "FElysiumNpcBase" and r["final_path"].startswith("FElysiumNpcBase::")
               for r in fields if r["disposition"] in ("bind", "save"))
    # The two renames step 3 recorded.
    by = {r["member"]: r for r in moves}
    assert by["BaseShouldPlayFloatSound"]["disposition"] == "rename:ShouldPlayFloatSound"
    assert by["BaseDrawDebugStatOverlays"]["disposition"] == "rename:DrawDebugStatOverlays"
