"""Story 5 step 4's move and binding manifests and their phase-4 source checks, on synthetic trees
plus the committed records."""
from __future__ import annotations

import collections
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research/tooling/ghidra/driver"))
sys.path.insert(0, str(REPO / "research/tooling"))

import kernel_migration as km  # noqa: E402
import kernel_migration_step4 as s4  # noqa: E402

BASES = {"FElysiumNpc": "FElysiumScriptedCharacter", "FElysiumNpcAnimal": "FElysiumNpc",
         "FElysiumNpcZombie": "FElysiumNpcAnimal", "FElysiumNpcDog": "FElysiumNpcAnimal",
         "FElysiumNpcHuman": "FElysiumNpc"}
SPECIES = {"FElysiumNpcAnimal", "FElysiumNpcZombie", "FElysiumNpcDog", "FElysiumNpcHuman"}


def write(root: Path, relative: Path | str, text: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def npc_tree(root: Path, inl: str, cpp: str, species: dict[str, str] | None = None) -> Path:
    write(root, s4.NPC_HEADER, "class FElysiumNpc : public FElysiumScriptedCharacter\n{\npublic:\n"
          "\tvirtual void NPCInit();\n\t#include \"Substrate/ElysiumNpcKernelFamily.inl\"\n};\n")
    write(root, s4.SUBSTRATE / "ElysiumNpcKernelFamily.inl", inl)
    write(root, s4.SUBSTRATE / "ElysiumNpcKernelFamily.cpp", cpp)
    for stem, text in (species or {}).items():
        write(root, s4.SUBSTRATE / stem, text)
    return root


def test_closest_common_is_the_nearest_shared_class():
    assert s4.closest_common(["FElysiumNpcZombie", "FElysiumNpcDog"], BASES) == "FElysiumNpcAnimal"
    assert s4.closest_common(["FElysiumNpcZombie"], BASES) == "FElysiumNpcZombie"
    assert s4.closest_common(["FElysiumNpcZombie", "FElysiumNpcHuman"], BASES) == "FElysiumNpc"


def test_members_count_overloads_in_every_declaring_family(tmp_path):
    root = npc_tree(tmp_path, "void Helper(int32 A);\nvoid Helper(float A);\nfloat ZombieWord = 0.f;\n"
                               "struct FRow { int32 A; };\n", "")
    members = s4.npc_members(root)
    assert members["Helper"]["kind"] == "method" and members["Helper"]["decls"]["ElysiumNpcKernelFamily.inl"] == 2
    assert members["ZombieWord"]["kind"] == "field"
    assert members["FRow"]["kind"] == "type"
    assert members["NPCInit"]["virtual"]


def test_a_member_only_species_use_moves_to_their_closest_class_through_other_moving_members(tmp_path):
    inl = "float ZombieWord = 0.f;\nvoid ZombieBody();\nvoid ZombieHelper();\nvoid SharedTroika();\n"
    cpp = ("void FElysiumNpc::ZombieBody() { ZombieHelper(); ZombieWord = 1.f; }\n"
           "void FElysiumNpc::ZombieHelper() { SharedTroika(); }\n"
           "void FElysiumNpc::SharedTroika() { }\n"
           "void FElysiumNpc::NPCInit() { SharedTroika(); }\n")
    species = {"ElysiumNpcZombie.cpp": "void FElysiumNpcZombie::NPCInit() { ZombieBody(); }\n"}
    root = npc_tree(tmp_path, inl, cpp, species)
    members = s4.npc_members(root)
    defs = s4.module_definitions(root)
    users = s4.member_users(root, members, defs)
    moving = s4.users_owner(members, users, SPECIES, BASES)
    assert moving == {"ZombieBody": "FElysiumNpcZombie", "ZombieHelper": "FElysiumNpcZombie",
                      "ZombieWord": "FElysiumNpcZombie"}


def test_a_header_or_free_function_use_keeps_a_member_on_the_npc(tmp_path):
    inl = "void Inline() { Kept(); }\nvoid Kept();\nvoid FreeKept();\n"
    cpp = ("static void Free(FElysiumNpc& N) { N.FreeKept(); }\n"
           "void FElysiumNpc::Kept() { }\nvoid FElysiumNpc::FreeKept() { }\n")
    species = {"ElysiumNpcZombie.cpp": "void FElysiumNpcZombie::NPCInit() { Kept(); FreeKept(); }\n"}
    root = npc_tree(tmp_path, inl, cpp, species)
    members = s4.npc_members(root)
    users = s4.member_users(root, members, s4.module_definitions(root))
    assert "<header:ElysiumNpcKernelFamily.inl>" in users["Kept"]
    assert "<free:ElysiumNpcKernelFamily.cpp>" in users["FreeKept"]
    assert s4.users_owner(members, users, SPECIES, BASES) == {}


def test_species_code_ranges():
    assert s4.species_code("103e1080") and s4.species_code("101aab90")      # Zombie, Payphone
    assert not s4.species_code("1027a530") and s4.base_code("1027a530")      # CAI_BaseNPC
    assert not s4.base_code("10449148") and not s4.species_code("10449148")  # a global, not a body


def test_species_datamaps_split_fields_inputs_outputs_and_skip_the_empty_terminator():
    classes = [{"retail_class": "CNPC_VZombie", "port_class": "FElysiumNpcZombie", "step": "2"},
               {"retail_class": "CNPC_VCamera", "port_class": "FElysiumNpcCamera", "step": "2"},
               {"retail_class": "CNPC_VWolfMorph", "port_class": "FElysiumNpcWolfMorph", "step": "7"}]
    record = lambda name, offset, typ, flags, names, ext: {
        "name": name, "typeName": typ, "offset": offset, "flags": flags, "flagNames": names, "external": ext}
    replay = {
        "CNPC_VZombie": {"records": [
            record("m_flRemoveDist", 0x66dc, "float", 6, ["SAVE", "KEY"], "remove_distance"),
            record("InputSetZombieAIType", 0, "string", 8, ["INPUT"], "SetZombieAIType"),
            record("m_OnAttackedVictim", 0x66e8, "custom", 22, ["SAVE", "KEY", "OUTPUT"], "OnAttackedVictim"),
            record("m_DeathDamageInfo", 0x668c, "embedded", 2, ["SAVE"], None)]},
        "CNPC_VCamera": {"records": [record(None, 0, "void", 0, [], None)]},
        "CNPC_VWolfMorph": {"records": [record("m_x", 0x6670, "int", 2, ["SAVE"], None)]},
    }
    rows = s4.species_datamaps(replay, classes)
    assert [(r["member"], r["kind"], r["external"]) for r in rows] == [
        ("InputSetZombieAIType", "input", "SetZombieAIType"), ("m_DeathDamageInfo", "field", "-"),
        ("m_flRemoveDist", "field", "remove_distance"), ("m_OnAttackedVictim", "output", "OnAttackedVictim")]


def _records(tmp_path: Path, columns, rows) -> Path:
    path = tmp_path / "records.tsv"
    s4.write_records(path, columns, rows, "test")
    return path


MOVE = {"member": "ZombieBody", "kind": "method", "declared_in": "ElysiumNpcKernelFamily.inl",
        "defined_in": "ElysiumNpcKernelFamily.cpp", "users_owner": "FElysiumNpcZombie", "retail_owner": "CNPC_VZombie",
        "final_owner": "FElysiumNpcZombie", "disposition": "move", "packet": "4d", "note": "-"}


def test_move_records_refuse_a_staying_member_off_the_npc_and_duplicates(tmp_path):
    assert s4.read_moves(_records(tmp_path, s4.MOVE_COLUMNS, [MOVE]))[0]["member"] == "ZombieBody"
    with pytest.raises(km.InvalidManifest):
        s4.read_moves(_records(tmp_path, s4.MOVE_COLUMNS, [{**MOVE, "disposition": "stay:troika api"}]))
    with pytest.raises(km.InvalidManifest):
        s4.read_moves(_records(tmp_path, s4.MOVE_COLUMNS, [MOVE, MOVE]))
    with pytest.raises(km.InvalidManifest):
        s4.read_moves(_records(tmp_path, s4.MOVE_COLUMNS, [{**MOVE, "disposition": "moved"}]))


def test_field_records_bind_inputs_and_outputs_by_kind(tmp_path):
    row = {"declaring_class": "CNPC_VZombie", "offset": "0x0000", "member": "InputSetZombieAIType",
           "type": "string", "flags": "8", "external": "SetZombieAIType", "kind": "input",
           "final_owner": "FElysiumNpcZombie", "final_path": "-", "disposition": "input-seam", "packet": "4b",
           "note": "-"}
    assert s4.read_fields(_records(tmp_path, s4.FIELD_COLUMNS, [row]))
    with pytest.raises(km.InvalidManifest):
        s4.read_fields(_records(tmp_path, s4.FIELD_COLUMNS, [{**row, "disposition": "bind"}]))


def test_phase4_moves_check_the_owner_and_the_npc(tmp_path):
    inl = "void Stays();\n"
    cpp = "void FElysiumNpc::Stays() { }\n"
    species = {"ElysiumNpcZombie.h": "class FElysiumNpcZombie { void ZombieBody(); };\n",
               "ElysiumNpcZombie.cpp": "void FElysiumNpcZombie::ZombieBody() { Stays(); }\n"}
    root = npc_tree(tmp_path, inl, cpp, species)
    stay = {**MOVE, "member": "Stays", "final_owner": "FElysiumNpc", "disposition": "stay:troika api", "packet": "-"}
    gone = {**MOVE, "member": "ZombieHelper", "disposition": "collapse:NPCInit"}
    assert s4.check_moves([MOVE, stay, gone], root) == collections.Counter({"move": 1, "stay": 1, "collapse": 1})
    with pytest.raises(km.InvalidManifest):
        s4.check_moves([{**MOVE, "final_owner": "FElysiumNpcDog"}], root)
    write(root, s4.SUBSTRATE / "ElysiumNpcKernelFamily.inl", "void Stays();\nvoid ZombieBody();\n")
    with pytest.raises(km.InvalidManifest):
        s4.check_moves([MOVE], root)


def test_the_committed_records_match_the_step3_tree_and_the_replay():
    manifest, classes, _ = km.load()
    assert manifest["history"]["step3"]["commit"]
    moves = s4.read_moves(REPO / km.STORY / "moves-step4.tsv")
    fields = s4.read_fields(REPO / km.STORY / "fields-step4.tsv")
    assert {r["declaring_class"] for r in fields} <= {r["retail_class"] for r in classes if r["step"] == "2"}
    assert sum(r["kind"] == "input" for r in fields) == 15 and sum(r["kind"] == "output" for r in fields) == 11
    assert all(r["member"] for r in moves)


def test_a_renamed_move_is_checked_under_its_new_spelling(tmp_path):
    species = {"ElysiumNpcZombie.h": "class FElysiumNpcZombie { bool bZombieHeadHit = false; };\n"}
    root = npc_tree(tmp_path, "void Stays();\n", "void FElysiumNpc::Stays() { }\n", species)
    word = {**MOVE, "member": "bZombieShouldGib", "kind": "field", "defined_in": "-",
            "note": "renamed to bZombieHeadHit: the +0x66e1 head-hit byte"}
    assert s4.check_moves([word], root) == collections.Counter({"move": 1})
    with pytest.raises(km.InvalidManifest):
        s4.check_moves([{**word, "note": "-"}], root)


def test_a_file_qualified_overlay_target_must_still_be_defined_in_its_file(tmp_path):
    def tree(root: Path, cpp: dict[str, str], overlay: str) -> Path:
        for name, text in cpp.items():
            write(root, f"Source/ElysiumUE/Private/Substrate/{name}", text)
        write(root, s4.VERDICTS, overlay)
        return root
    before = tree(tmp_path / "before", {"ElysiumNpc.cpp": "void FElysiumNpc::Moved()\n{\n}\n"}, "")
    row = "103871c0\tpresent\t5-9\t{target}\tevidence\n"
    moved = {"ElysiumNpcHuman.cpp": "void FElysiumNpcHuman::Moved()\n{\n}\n"}
    stale = tree(tmp_path / "stale", moved, row.format(target="ElysiumNpc.cpp:FElysiumNpc::Moved"))
    with pytest.raises(km.InvalidManifest, match="103871c0"):
        s4.check_qualified_overlay_targets(before, stale)
    fixed = tree(tmp_path / "fixed", moved, row.format(target="ElysiumNpcHuman.cpp:FElysiumNpcHuman::Moved"))
    assert s4.check_qualified_overlay_targets(before, fixed) == 1
