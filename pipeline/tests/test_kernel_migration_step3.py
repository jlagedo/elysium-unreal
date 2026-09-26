"""Story 5 step 3's override matrix, static dispatch gate and direct-call checks, on synthetic trees
plus the committed record."""
from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research/tooling/ghidra/driver"))
sys.path.insert(0, str(REPO / "research/tooling"))

import kernel_migration as km  # noqa: E402
import kernel_migration_step3 as s3  # noqa: E402

CLASSES = [
    {"retail_class": "CAI_BaseNPCTroika", "port_class": "FElysiumNpc", "step": "2"},
    {"retail_class": "CNPC_VAnimal", "port_class": "FElysiumNpcAnimal", "step": "2"},
    {"retail_class": "CNPC_VZombie", "port_class": "FElysiumNpcZombie", "step": "2"},
    {"retail_class": "CNPC_VDog", "port_class": "FElysiumNpcDog", "step": "2"},
    {"retail_class": "CNPC_VPlayerController", "port_class": "FElysiumNpcPlayerController", "step": "7"},
]


def write(root: Path, relative: Path | str, text: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def tree(root: Path, overrides: list[tuple[str, int, str]], verdicts: dict[str, tuple[str, str]],
         source: str = "") -> Path:
    classes = [("CAI_BaseNPCTroika", "CAI_BaseNPC"), ("CNPC_VAnimal", "CAI_BaseNPCTroika"),
               ("CNPC_VZombie", "CNPC_VAnimal"), ("CNPC_VDog", "CNPC_VAnimal"),
               ("CNPC_VPlayerController", "CAI_BaseNPCTroika")]
    slots = [(482, "", "CanPlaySequence"), (510, "", "ShouldPlayFloatSound"),
             (599, "CAI_BaseNPCTroika", "Slot599"), (599, "CNPC_VAnimal", "AnimalOwn599")]
    text = "\tconstexpr FElysiumNpcSlot GSlots[] =\n\t{\n" + "".join(
        f'\t\t{{ {s}, TEXT("{c}"), TEXT(""), TEXT("bool X(int)"),\n\t\t\tTEXT("{m}"), ETier::Walked, '
        f'TEXT(""), 0, TEXT(""), false, TEXT(""), TEXT("") }},\n' for s, c, m in slots) + "\t};\n"
    text += "\tconstexpr FElysiumNpcClass GClasses[] =\n\t{\n" + "".join(
        f'\t\t{{ TEXT("{c}"), TEXT("{b}"), TEXT("0x10000000"), 617, 1,\n\t\t\tnullptr, 0 }},\n'
        for c, b in classes) + "\t};\n"
    text += "\tconstexpr FElysiumNpcClassSlot GOverrides[] =\n\t{\n" + "".join(
        f'\t\t{{ TEXT("{c}"), {s}, TEXT("{a}"), TEXT("m"), TEXT("rule"), TEXT("") }},\n'
        for c, s, a in overrides) + "\t};\n"
    write(root, s3.SHAPE, text)
    write(root, s3.VERDICTS, "address\tverdict\tband\ttarget\tevidence\n" + "".join(
        f"{a}\t{v}\t0-4\t{t}\tx\n" for a, (v, t) in verdicts.items()))
    write(root, s3.SUBSTRATE / "ElysiumNpcKernelSpecies.cpp", source)
    return root


def test_a_row_belongs_to_the_topmost_class_holding_its_body(tmp_path):
    # Zombie and Dog repeat the Animal body at 482 (vtable copies): one Animal row. Zombie's own
    # 510 body is its own row; a deferred class's row is not introduced.
    root = tree(tmp_path,
                [("CNPC_VAnimal", 482, "0x1035fd40"), ("CNPC_VZombie", 482, "0x1035fd40"),
                 ("CNPC_VDog", 482, "0x1035fd40"), ("CNPC_VZombie", 510, "0x103e1080"),
                 ("CNPC_VPlayerController", 482, "0x103850a0")],
                {"1035fd40": ("rule", "FElysiumNpc::FUN_1035fd40"),
                 "103e1080": ("rule", "FElysiumNpc::FUN_103e1080"),
                 "103850a0": ("rule", "FElysiumNpc::FUN_103850a0")},
                "bool FElysiumNpc::FUN_1035fd40() { return 1; }\n"
                "bool FElysiumNpc::FUN_103e1080() { return 1; }\n")
    rows = s3.override_matrix(root, CLASSES)
    assert [(r["retail_class"], r["slot"], r["port_class"], r["port_method"]) for r in rows] == [
        ("CNPC_VAnimal", "482", "FElysiumNpcAnimal", "CanPlaySequence"),
        ("CNPC_VZombie", "510", "FElysiumNpcZombie", "ShouldPlayFloatSound")]


def test_membership_needs_a_run_body_or_an_address_arm(tmp_path):
    # A dead body with no arm, and a live verdict whose target the port does not define, stay out;
    # a dead body the hand source still dispatches on its address comes in.
    root = tree(tmp_path,
                [("CNPC_VAnimal", 482, "0x10000001"), ("CNPC_VAnimal", 510, "0x10000002"),
                 ("CNPC_VDog", 510, "0x10000003")],
                {"10000001": ("dead", "-"), "10000002": ("rule", "FElysiumNpc::Missing"),
                 "10000003": ("dead", "-")},
                'void FElysiumNpc::Arm() { if (FCString::Strcmp(Body, TEXT("0x10000003")) == 0) {} }\n')
    assert [(r["retail_class"], r["address"], r["verdict"]) for r in s3.override_matrix(root, CLASSES)] == [
        ("CNPC_VDog", "0x10000003", "dead")]


def test_a_branch_slot_takes_the_port_method_its_introducing_class_declares(tmp_path):
    root = tree(tmp_path, [("CNPC_VZombie", 599, "0x10000004")], {"10000004": ("rule", "FElysiumNpc::Own")},
                "void FElysiumNpc::Own() {}\n")
    assert s3.override_matrix(root, CLASSES)[0]["port_method"] == "AnimalOwn599"


def test_an_override_row_must_be_a_declared_override_defined_in_its_class(tmp_path):
    row = {"retail_class": "CNPC_VZombie", "slot": "510", "port_class": "FElysiumNpcZombie",
           "disposition": "override:FElysiumNpcZombie::ShouldPlayFloatSound"}
    write(tmp_path, s3.SUBSTRATE / "ElysiumNpcZombie.h",
          "class FElysiumNpcZombie : public FElysiumNpcAnimal\n{\n\tvirtual bool ShouldPlayFloatSound();\n};\n")
    write(tmp_path, s3.SUBSTRATE / "ElysiumNpcZombie.cpp",
          "bool FElysiumNpcZombie::ShouldPlayFloatSound() { return 0; }\n")
    with pytest.raises(km.InvalidManifest, match="does not declare"):
        s3.check_overrides([row], tmp_path)
    write(tmp_path, s3.SUBSTRATE / "ElysiumNpcZombie.h",
          "class FElysiumNpcZombie : public FElysiumNpcAnimal\n{\n"
          "\tvirtual bool ShouldPlayFloatSound() override;\n};\n")
    assert s3.check_overrides([row], tmp_path) == 1
    with pytest.raises(km.InvalidManifest, match="introducing class"):
        s3.check_overrides([{**row, "port_class": "FElysiumNpcAnimal"}], tmp_path)


SOURCE = """
bool FElysiumNpc::ShouldPlayFloatSound()
{
	// OverrideOf in a comment is not a site
	const FElysiumNpcClassSlot* Row = OverrideOf(RetailClass(), 510);
	if (Row && FCString::Strcmp(Row->Address, TEXT("0x103e1080")) == 0) { return true; }
	if (FCString::Strcmp(Name, TEXT("npc_VRat")) == 0) { return false; }
	return IsRetailClass(TEXT("CNPC_VZombie"));
}
static const FArm GArms[] = { { TEXT("0x10363940"), &FElysiumNpc::BachNPCInit } };
"""


def test_the_gate_counts_dispatch_tokens_per_definition(tmp_path):
    write(tmp_path, s3.SUBSTRATE / "ElysiumNpcKernelSounds.cpp", SOURCE)
    write(tmp_path, s3.PRIVATE / "Tests/ElysiumNpcKernelSoundsTests.cpp", "OverrideOf(RetailClass(), 510);")
    file = (s3.SUBSTRATE / "ElysiumNpcKernelSounds.cpp").as_posix()
    assert s3.gate_sites(tmp_path) == {
        (file, "FElysiumNpc::ShouldPlayFloatSound", "OverrideOf"): 1,
        (file, "FElysiumNpc::ShouldPlayFloatSound", "Strcmp"): 1,
        (file, "FElysiumNpc::ShouldPlayFloatSound", "IsRetailClass"): 1,
        (file, "<file scope>", "&FElysiumNpc::"): 1}
    survivors = {"surviving_sites": [
        {"file": file, "symbol": "FElysiumNpc::ShouldPlayFloatSound", "token": t, "count": 1,
         "disposition": "deferred", "removal_step": 7, "reason": "x"}
        for t in ("OverrideOf", "Strcmp", "IsRetailClass")]}
    with pytest.raises(km.InvalidManifest, match="unlisted"):
        s3.check_gate(survivors, tmp_path)
    survivors["surviving_sites"].append({"file": file, "symbol": "<file scope>", "token": "&FElysiumNpc::",
                                         "count": 1, "disposition": "deferred", "removal_step": 7,
                                         "reason": "x"})
    assert s3.check_gate(survivors, tmp_path) == 4
    survivors["surviving_sites"][0]["removal_step"] = 11
    with pytest.raises(km.InvalidManifest, match="fold step"):
        s3.check_gate(survivors, tmp_path)


def test_a_direct_call_is_spelled_in_its_caller(tmp_path):
    write(tmp_path, s3.SUBSTRATE / "ElysiumNpcZombie.cpp",
          "bool FElysiumNpcZombie::ShouldPlayFloatSound()\n{\n\treturn ShouldPlayFloatSound();\n}\n")
    call = {"caller": "FElysiumNpcZombie::ShouldPlayFloatSound", "required": "BaseShouldPlayFloatSound()",
            "retail": {"site": "0x103e11f9", "callee": "0x1027a530"}}
    with pytest.raises(km.InvalidManifest, match="does not call"):
        s3.check_direct_calls({"direct_calls": [call]}, tmp_path)
    write(tmp_path, s3.SUBSTRATE / "ElysiumNpcZombie.cpp",
          "bool FElysiumNpcZombie::ShouldPlayFloatSound()\n{\n\treturn BaseShouldPlayFloatSound();\n}\n")
    assert s3.check_direct_calls({"direct_calls": [call]}, tmp_path) == 1


def test_the_committed_matrix_matches_the_accepted_step2_census():
    manifest, _, _ = km.load()
    rows = s3.read_matrix(REPO / km.STORY / "overrides-step3.tsv")
    assert len(rows) == 611
    decisions = json.loads((REPO / km.STORY / "decisions-step3.json").read_text(encoding="utf-8"))
    assert {"rules", "direct_calls", "surviving_sites", "step5_renames"} <= set(decisions)
    if manifest["phase"] < 3:
        counts = s3.check_step3()
        assert counts["pending"] and counts["matrix_rows"] == 611
