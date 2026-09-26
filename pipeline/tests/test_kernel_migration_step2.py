"""Story 5 step 2's factory census, class tree, registrar and latch checks, on synthetic trees plus
the committed factory map."""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research/tooling/ghidra/driver"))
sys.path.insert(0, str(REPO / "research/tooling"))

import kernel_migration as km  # noqa: E402
import kernel_migration_step2 as s2  # noqa: E402

CLASSES = [
    {"retail_class": "CAI_BaseNPCTroika", "retail_base": "CAI_BaseNPC", "port_class": "FElysiumNpc",
     "port_base": "FElysiumNpcBase", "step": "2"},
    {"retail_class": "CNPC_VHuman", "retail_base": "CAI_BaseNPCTroika", "port_class": "FElysiumNpcHuman",
     "port_base": "FElysiumNpc", "step": "2"},
    {"retail_class": "CNPC_VPedestrian", "retail_base": "CNPC_VHuman",
     "port_class": "FElysiumNpcPedestrian", "port_base": "FElysiumNpcHuman", "step": "2"},
    {"retail_class": "CNPCMaker", "retail_base": "CAI_BaseNPCTroika", "port_class": "FElysiumNpcMaker",
     "port_base": "FElysiumNpc", "step": "8"},
]
FACTORIES = [
    {"classname": "npc_VHuman", "retail_class": "CNPC_VHuman"},
    {"classname": "npc_VDialogPedestrian", "retail_class": "CNPC_VPedestrian"},
    {"classname": "npc_VPedestrian", "retail_class": "CNPC_VPedestrian"},
    {"classname": "npc_maker", "retail_class": "CNPCMaker"},
]


def write(root: Path, relative: Path | str, text: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def census(root: Path, arrays: dict[str, list[str]], rows: dict[str, tuple[str, int]] | None = None) -> None:
    text = "\n".join(
        f"\tconstexpr const TCHAR* {cls}_Classnames[] =\n\t\t{{ "
        + ", ".join(f'TEXT("{n}")' for n in names) + " };"
        for cls, names in arrays.items())
    if rows is None:
        rows = {cls: (f"{cls}_Classnames", len(names)) for cls, names in arrays.items()}
    text += "\n" + "\n".join(
        f'\t\t{{ TEXT("{cls}"), TEXT("CAI_BaseNPCTroika"), TEXT("0x10000000"), 617, 1,\n'
        f"\t\t\t{array}, {count} }},"
        for cls, (array, count) in rows.items())
    write(root, s2.SHAPE, text)


def test_census_classnames_must_equal_the_factories(tmp_path):
    census(tmp_path, {"CNPC_VHuman": ["npc_VHuman"],
                      "CNPC_VPedestrian": ["npc_VPedestrian", "npc_VDialogPedestrian"],
                      "CNPCMaker": ["npc_maker"]})
    assert s2.check_census(FACTORIES, tmp_path) == 4
    # A base claiming its descendant's name (the proximity over-claim) refuses.
    census(tmp_path, {"CNPC_VHuman": ["npc_VHuman", "npc_VPedestrian"],
                      "CNPC_VPedestrian": ["npc_VPedestrian", "npc_VDialogPedestrian"],
                      "CNPCMaker": ["npc_maker"]})
    with pytest.raises(km.InvalidManifest, match="CNPC_VHuman"):
        s2.check_census(FACTORIES, tmp_path)
    # A class the census gives no classname (the old npc_VCop gap) refuses too.
    census(tmp_path, {"CNPC_VHuman": ["npc_VHuman"], "CNPCMaker": ["npc_maker"]})
    with pytest.raises(km.InvalidManifest, match="CNPC_VPedestrian"):
        s2.check_census(FACTORIES, tmp_path)


def shell(root: Path, port: str, base: str, retail: str) -> None:
    stem = port[1:]
    write(root, s2.SUBSTRATE / f"{stem}.h",
          f"#pragma once\n// class {port} : public Nothing (a comment is not the declaration)\n"
          f"class {port} : public {base}\n{{\npublic:\n\tvirtual const FElysiumNpcClass* "
          "OwnRetailClass() const override;\n};\n")
    write(root, s2.SUBSTRATE / f"{stem}.cpp",
          f'const FElysiumNpcClass* {port}::OwnRetailClass() const\n{{\n\tstatic const '
          f'FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("{retail}"));\n'
          "\treturn Row;\n}\n")


def test_each_species_class_stands_with_its_parent_and_answers_its_row(tmp_path):
    shell(tmp_path, "FElysiumNpcHuman", "FElysiumNpc", "CNPC_VHuman")
    shell(tmp_path, "FElysiumNpcPedestrian", "FElysiumNpcHuman", "CNPC_VPedestrian")
    assert s2.check_shells(CLASSES, tmp_path) == 2
    shell(tmp_path, "FElysiumNpcPedestrian", "FElysiumNpc", "CNPC_VPedestrian")
    with pytest.raises(km.InvalidManifest, match="does not derive"):
        s2.check_shells(CLASSES, tmp_path)
    shell(tmp_path, "FElysiumNpcPedestrian", "FElysiumNpcHuman", "CNPC_VHuman")
    with pytest.raises(km.InvalidManifest, match="does not answer"):
        s2.check_shells(CLASSES, tmp_path)


REGISTRAR = """
static const FElysiumNpcRetailClassRow GNpcRetailClasses[] =
{
	{ TEXT("CNPC_VHuman"), TEXT("CAI_BaseNPCTroika") },
	{ TEXT("CNPC_VPedestrian"), TEXT("CNPC_VHuman") },
};

static const FElysiumNpcClassnameRow GNpcClassnames[] =
{
	{ TEXT("npc_VDialogPedestrian"), TEXT("CNPC_VPedestrian"), &MakeNpcOf<FElysiumNpcPedestrian> },
	{ TEXT("npc_VHuman"), TEXT("CNPC_VHuman"), &MakeNpcOf<FElysiumNpcHuman> },
	{ TEXT("npc_VPedestrian"), TEXT("CNPC_VPedestrian"),
		&MakeNpcOf<FElysiumNpcPedestrian> },
};
		BuildNpcClass(Reg.RegisterAbstract(TEXT("CAI_BaseNPCTroika"), ElysiumCombatCharacterClassName()));
		for (const FElysiumNpcRetailClassRow& Row : GNpcRetailClasses)
		{
			FElysiumClassDesc& D = Reg.RegisterAbstract(FName(Row.RetailClass), FName(Row.RetailBase));
		}
		for (const FElysiumNpcClassnameRow& Row : GNpcClassnames)
		{
			Reg.Register(FName(Row.Classname), FName(Row.RetailClass), Row.Factory);
		}
		static const TCHAR* const MakerClasses[] = { TEXT("npc_maker"), TEXT("npc_maker_fleshpile") };
"""


def test_the_registrar_registers_exactly_the_typed_classnames(tmp_path):
    write(tmp_path, s2.REGISTRAR, REGISTRAR)
    assert s2.check_registrar(CLASSES, FACTORIES, tmp_path) == 3
    write(tmp_path, s2.REGISTRAR, REGISTRAR.replace(
        "&MakeNpcOf<FElysiumNpcHuman>", "&MakeNpcOf<FElysiumNpcPedestrian>"))
    with pytest.raises(km.InvalidManifest, match="registered classnames differ"):
        s2.check_registrar(CLASSES, FACTORIES, tmp_path)
    write(tmp_path, s2.REGISTRAR, REGISTRAR.replace(
        '{ TEXT("CNPC_VPedestrian"), TEXT("CNPC_VHuman") }', '{ TEXT("CNPC_VPedestrian"), TEXT("CAI_BaseNPCTroika") }'))
    with pytest.raises(km.InvalidManifest, match="abstract retail descriptors differ"):
        s2.check_registrar(CLASSES, FACTORIES, tmp_path)
    write(tmp_path, s2.REGISTRAR, REGISTRAR + '\tReg.Register(FName(Name), Base, &MakeNpc);\n')
    with pytest.raises(km.InvalidManifest, match="untyped shared NPC leaf"):
        s2.check_registrar(CLASSES, FACTORIES, tmp_path)


def test_the_latch_survives_only_at_listed_deferred_sites(tmp_path):
    write(tmp_path, s2.TESTS / "ElysiumNpcXTests.cpp",
          '// Npc->SetRetailClassForTests(TEXT("CNPC_VCop")); a comment is not a site\n'
          'Npc->SetRetailClassForTests(\n\t\tTEXT("CNPC_VFrenzyShadow"));\n')
    write(tmp_path, s2.SUBSTRATE / "ElysiumNpcKernelSpecies.cpp",
          'void FElysiumNpc::SetRetailClassForTests(const TCHAR* RetailClassName)\n{\n}\n')
    listed = {"compatibility": {"set_retail_class_for_tests": [
        {"file": "Private/Tests/ElysiumNpcXTests.cpp", "argument": 'TEXT("CNPC_VFrenzyShadow")',
         "classes": ["CNPC_VFrenzyShadow"]}]}}
    # A call split across lines is a site; the definition is not.
    assert s2.check_latches(listed, tmp_path) == 1
    # A second call with the same argument is a new site, not the listed one.
    write(tmp_path, s2.TESTS / "ElysiumNpcXTests.cpp",
          'Npc->SetRetailClassForTests(TEXT("CNPC_VFrenzyShadow"));\n'
          'Other->SetRetailClassForTests(TEXT("CNPC_VFrenzyShadow"));\n')
    with pytest.raises(km.InvalidManifest, match="unlisted"):
        s2.check_latches(listed, tmp_path)
    write(tmp_path, s2.TESTS / "ElysiumNpcXTests.cpp",
          'Npc->SetRetailClassForTests(TEXT("CNPC_VFrenzyShadow"));\n')
    # A species class latched on anything refuses, whatever the record claims for it.
    write(tmp_path, s2.TESTS / "ElysiumNpcYTests.cpp", 'Npc->SetRetailClassForTests(TEXT("CNPC_VCop"));\n')
    listed["compatibility"]["set_retail_class_for_tests"].append(
        {"file": "Private/Tests/ElysiumNpcYTests.cpp", "argument": 'TEXT("CNPC_VCop")',
         "classes": ["CNPC_VFrenzyShadow"]})
    with pytest.raises(km.InvalidManifest, match="non-deferred"):
        s2.check_latches(listed, tmp_path)
    (tmp_path / s2.TESTS / "ElysiumNpcYTests.cpp").unlink()
    listed["compatibility"]["set_retail_class_for_tests"].pop()
    # Production code may not call it at all.
    write(tmp_path, s2.SUBSTRATE / "ElysiumNpcFoo.cpp", 'Npc->SetRetailClassForTests(nullptr);\n')
    with pytest.raises(km.InvalidManifest, match="production code"):
        s2.check_latches(listed, tmp_path)


def test_a_census_row_must_bind_its_own_array(tmp_path):
    arrays = {"CNPC_VHuman": ["npc_VHuman"],
              "CNPC_VPedestrian": ["npc_VPedestrian", "npc_VDialogPedestrian"],
              "CNPCMaker": ["npc_maker"]}
    census(tmp_path, arrays, {"CNPC_VHuman": ("CNPC_VHuman_Classnames", 1),
                              "CNPC_VPedestrian": ("CNPC_VPedestrian_Classnames", 1),
                              "CNPCMaker": ("CNPCMaker_Classnames", 1)})
    with pytest.raises(km.InvalidManifest, match="binds"):
        s2.check_census(FACTORIES, tmp_path)
    census(tmp_path, arrays, {"CNPC_VHuman": ("CNPCMaker_Classnames", 1),
                              "CNPC_VPedestrian": ("CNPC_VPedestrian_Classnames", 2),
                              "CNPCMaker": ("CNPCMaker_Classnames", 1)})
    with pytest.raises(km.InvalidManifest, match="binds"):
        s2.check_census(FACTORIES, tmp_path)


def test_the_committed_registration_record_matches_the_factory_map():
    _, classes, factories = km.load()
    counts = s2.check_registrations(classes, factories, REPO / km.STORY / "registrations-step2.tsv",
                                    require_smoke=False)
    assert counts == {"newly-active": 30, "corrected": 6}


def test_the_committed_tree_passes_the_structural_checks():
    _, classes, factories = km.load()
    assert s2.check_census(factories, REPO) == 74
    assert s2.check_shells(classes, REPO) == 44
    assert s2.check_registrar(classes, factories, REPO) == 45
