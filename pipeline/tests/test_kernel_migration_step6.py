"""Story 5 step 6's slot and member records and their phase-6 source checks, on synthetic trees plus
the committed records."""
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
import kernel_migration_step6 as s6  # noqa: E402


def write(root: Path, relative: Path | str, text: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


INL = """// header
\t// slot 8 0x100b17f0 (sdk) `int GetModelIndex() const`
\t//   layer 0, story 29c
\tvirtual int32 GetModelIndex() const;
\t// slot 25 0x100265b0 (walked) `void vfunc25(CBaseEntity*)`
\t//   layer 0, story 29c
\tvirtual void Slot25(FElysiumEntity*);
\t// slot 17 0x1028de90 (walked) `void TraceMessage(const char*, int) const`
\tvoid TraceMessage(const TCHAR*, int32) const override;
"""
CPP = """// slot 8 0x100b17f0 (sdk) `int GetModelIndex() const`
//   layer 0, story 29c
int32 FElysiumNpcBase::GetModelIndex() const
{
\tFireKernelBaseSlot(TEXT("GetModelIndex"), TEXT("0x100b17f0"), TEXT("29c"), DebugString());
\treturn {};
}

// slot 25 0x100265b0 (walked) `void vfunc25(CBaseEntity*)`
//   layer 0, story 29c
// verdict `rule`: the body is `FElysiumNpcBase::Slot25`, written by hand in the substrate. Declared
// here, defined there.

// slot 17 0x1028de90 (walked) `void TraceMessage(const char*, int) const`
// verdict `rule`: retail's whole body is `return;`
void FElysiumNpc::TraceMessage(const TCHAR*, int32) const
{
}
"""


def row(**values) -> dict:
    base = {"slot": "8", "port_name": "GetModelIndex", "signature": "int32 GetModelIndex() const",
            "current_owner": "FElysiumNpcBase", "row_class": "CBaseEntity", "body": "0x100b17f0",
            "kind": "stub", "verdict": "-", "introducer": "CBaseEntity", "final_owner": "FElysiumEntity",
            "disposition": "move", "packet": "6c", "note": "-"}
    return {**base, **values}


def write_slots(path: Path, rows: list[dict]) -> Path:
    with path.open("w", encoding="utf-8", newline="\n") as out:
        out.write("# test\n")
        writer = csv.DictWriter(out, s6.SLOT_COLUMNS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    return path


def test_declarations_and_kinds_are_read_off_the_generated_surface():
    decls = s6._decls(INL)
    assert decls == [(8, "0x100b17f0", "int32 GetModelIndex() const", False),
                     (25, "0x100265b0", "void Slot25(FElysiumEntity*)", False),
                     (17, "0x1028de90", "void TraceMessage(const TCHAR*, int32) const", True)]
    kinds = s6._kinds(CPP)
    # the hand verdict wraps across two comment lines and is still read as hand
    assert kinds == {(8, "0x100b17f0"): "stub", (25, "0x100265b0"): "hand", (17, "0x1028de90"): "default"}


def test_read_slots_holds_a_row_to_its_retail_owner(tmp_path):
    good = write_slots(tmp_path / "ok.tsv", [row()])
    assert s6.read_slots(good)[0]["final_owner"] == "FElysiumEntity"
    wrong = write_slots(tmp_path / "wrong.tsv", [row(final_owner="FElysiumCombatCharacter")])
    with pytest.raises(km.InvalidManifest, match="belongs on FElysiumEntity"):
        s6.read_slots(wrong)


def test_toggle_bodies_stand_on_the_animating_class(tmp_path):
    path = write_slots(tmp_path / "t.tsv", [row(slot="133", row_class="CBaseToggle", body="0x101c1720",
                                                final_owner="FElysiumAnimating")])
    assert s6.read_slots(path)


def test_only_a_dead_stub_is_deleted_and_a_new_row_stands_on_its_class(tmp_path):
    live = write_slots(tmp_path / "live.tsv", [row(disposition="delete-dead", packet="6f")])
    with pytest.raises(km.InvalidManifest, match="only a dead stub"):
        s6.read_slots(live)
    new = write_slots(tmp_path / "new.tsv", [row(current_owner="-", kind="new", disposition="new-chain-row",
                                                 row_class="CBaseAnimating", final_owner="FElysiumEntity")])
    with pytest.raises(km.InvalidManifest, match="new chain row"):
        s6.read_slots(new)


def test_move_hand_needs_a_hand_row(tmp_path):
    path = write_slots(tmp_path / "h.tsv", [row(disposition="move-hand", packet="6d")])
    with pytest.raises(km.InvalidManifest, match="move-hand"):
        s6.read_slots(path)


def test_phase6_slot_check_finds_the_row_on_its_owner_only(tmp_path):
    entity = "\t// slot 8 0x100b17f0 (sdk) `int GetModelIndex() const`\n\tvirtual int32 GetModelIndex() const;\n"
    for cls, (inl, _) in s6.SLOT_FILES.items():
        write(tmp_path, inl, entity if cls == "FElysiumEntity" else "")
    write(tmp_path, s6.VERDICTS, "")
    assert s6.check_slots([row()], tmp_path) == collections.Counter({"move": 1})
    write(tmp_path, s6.SLOT_FILES["FElysiumNpcBase"][0], entity)
    with pytest.raises(km.InvalidManifest, match="still declared on FElysiumNpcBase"):
        s6.check_slots([row()], tmp_path)


def test_phase6_member_check_needs_the_final_scope(tmp_path):
    write(tmp_path, s6.CLASS_HEADERS["FElysiumEntity"], "class FElysiumEntity\n{\npublic:\n\tint32 Flinch = 0;\n};\n")
    write(tmp_path, s6.CLASS_HEADERS["FElysiumNpcBase"],
          "class FElysiumNpcBase : public FElysiumScriptedCharacter\n{\npublic:\n"
          "\t#include \"Substrate/ElysiumNpcBaseAnim.inl\"\n};\n")
    write(tmp_path, s6.SUBSTRATE / "ElysiumNpcBaseAnim.inl", "int32 Other = 0;\n")
    move = {"member": "Flinch", "kind": "field", "current_owner": "FElysiumNpcBase", "declared_in": "x",
            "defined_in": "-", "final_owner": "FElysiumEntity", "disposition": "move", "packet": "6d", "note": "-"}
    assert s6.check_moves([move], tmp_path)["FElysiumEntity"] == 1
    write(tmp_path, s6.SUBSTRATE / "ElysiumNpcBaseAnim.inl", "int32 Flinch = 0;\n")
    with pytest.raises(km.InvalidManifest, match="still declared on FElysiumNpcBase"):
        s6.check_moves([move], tmp_path)


def test_retired_tables_refuse(tmp_path):
    write(tmp_path, s6.SUBSTRATE / "ElysiumNpcSounds.cpp", "static const FVocalization GSoundsVocalizations[] = {};\n")
    with pytest.raises(km.InvalidManifest, match="retired tables"):
        s6.check_retired(tmp_path)
    write(tmp_path, s6.SUBSTRATE / "ElysiumNpcSounds.cpp", "// FVocalization retired in step 6\n")
    assert s6.check_retired(tmp_path) == 0


def test_the_committed_records_are_complete_and_reviewed():
    directory = REPO / km.STORY
    manifest, _, _ = km.load(directory)
    assert manifest["history"]["step5"]["commit"]
    slots = s6.read_slots(directory / "slots-step6.tsv")
    moves = s6.read_moves(directory / "moves-step6.tsv")
    assert not [r for r in slots if r["disposition"] == "investigate"]
    assert len(slots) == 824 and len(moves) == 130
    counts = collections.Counter(r["disposition"].split(":")[0] for r in slots)
    assert counts == {"keep": 374, "move": 256, "new-chain-row": 102, "move-hand": 84, "adapter": 5,
                      "seam": 1, "implemented": 1, "delete-dead": 1}
    # every chain-owned base row leaves the NPC base; the Troika rows all stay
    assert all(r["final_owner"] == "FElysiumNpc" for r in slots if r["current_owner"] == "FElysiumNpc")
