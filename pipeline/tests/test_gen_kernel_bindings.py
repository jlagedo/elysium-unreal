"""The datamap-binding generator's species half (0019 story 5 step 4), on synthetic replay tables and
species shape maps, plus an oracle over the committed map and the pinned replay when it is present.

A species word is keyed by the class that declares it and its offset: siblings reuse offsets freely
(`+0x6664` is fourteen different words), so the generator must bind each row on its declaring class
only, refuse a map that names a key twice or binds one port member for two words, and refuse a
datamap field the map does not answer.
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling"))
sys.path.insert(0, str(REPO / "research" / "tooling" / "ghidra" / "driver"))

import gen_kernel_bindings as gkb  # noqa: E402


def record(name, offset, type_name="int", flags=("SAVE",), external=None, count=1):
    bits = {"SAVE": 2, "KEY": 4, "INPUT": 8, "OUTPUT": 16}
    return {"name": name, "typeName": type_name, "offset": offset, "count": count,
            "flags": sum(bits[f] for f in flags), "flagNames": list(flags), "external": external}


def replay(**tables):
    return {cls: {"datamap": "0x0", "base": "CAI_BaseNPCTroika", "records": rows}
            for cls, rows in tables.items()}


MAP = """
    ELYSIUM_NPC_SPECIES_WORD(CNPC_VAnimal, 0x6664, FElysiumNpcAnimal, AnimalFriendshipLevel),
    ELYSIUM_NPC_SPECIES_WORD(CNPC_VCop, 0x6664, FElysiumNpcCop, CopPursuitHandle),
    ELYSIUM_NPC_SPECIES_WORD_NOTED(CNPC_VZombie, 0x6678, FElysiumNpc, ZombieAiType,
        "deferred:8"),
    ELYSIUM_NPC_SPECIES_WORD_SHADOW(CNPC_VTzimisce, 0x6458, IgnoreCollisionUntil),
    ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x668c, FElysiumNpcMingXiao, Proxies),
    ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VBach, 0x6664,  // m_vecLastOccludeOrigin
        "no port member"),
"""


def test_species_map_keys_a_word_by_class_and_offset():
    rows = gkb.parse_species_map(MAP)
    # One offset, two classes, two different words.
    assert rows[("CNPC_VAnimal", 0x6664)] == ("member", "FElysiumNpcAnimal", "AnimalFriendshipLevel")
    assert rows[("CNPC_VCop", 0x6664)] == ("member", "FElysiumNpcCop", "CopPursuitHandle")
    assert rows[("CNPC_VBach", 0x6664)] == ("absent", "", "")
    assert rows[("CNPC_VTzimisce", 0x6458)] == ("shadow", "FElysiumNpc", "IgnoreCollisionUntil")
    assert rows[("CNPC_VZombie", 0x6678)] == ("member", "FElysiumNpc", "ZombieAiType")


def test_species_map_refuses_a_key_named_twice():
    text = MAP + "ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VAnimal, 0x6664, \"again\"),"
    with pytest.raises(SystemExit, match="names CNPC_VAnimal \\+0x6664 twice"):
        gkb.parse_species_map(text)


def test_species_map_refuses_one_member_for_two_words():
    text = MAP + "ELYSIUM_NPC_SPECIES_WORD(CNPC_VHunter, 0x6664, FElysiumNpcCop, CopPursuitHandle),"
    with pytest.raises(SystemExit, match="bound for both"):
        gkb.parse_species_map(text)


def test_shadow_rows_may_share_inherited_storage():
    text = MAP + "ELYSIUM_NPC_SPECIES_WORD_SHADOW(CNPC_VHengeyokai, 0x6458, IgnoreCollisionUntil),"
    assert gkb.parse_species_map(text)[("CNPC_VHengeyokai", 0x6458)][0] == "shadow"


def test_a_species_row_binds_on_its_declaring_class_only():
    data = replay(CNPC_VAnimal=[record("m_iFriendshipLevel", 0x6664, flags=("SAVE", "KEY"),
                                       external="friendship_level")],
                  CNPC_VCop=[record("m_hPursuitPlayer", 0x6664, "ehandle")])
    species = gkb.parse_species_map(MAP)
    animal = gkb.classify_species("Animal", "CNPC_VAnimal", data, species)
    cop = gkb.classify_species("Cop", "CNPC_VCop", data, species)
    # The keyed row registers under its external; the SAVE-only row under its retail name.
    assert [(r.external, r.binding) for r in animal.bound] == [
        ("friendship_level", ("FElysiumNpcAnimal", "AnimalFriendshipLevel"))]
    assert animal.saved == []
    assert [(r.name, r.binding) for r in cop.saved] == [
        ("m_hPursuitPlayer", ("FElysiumNpcCop", "CopPursuitHandle"))]
    assert gkb.flags_of(animal.bound[0]) == "EElysiumField::Save"


def test_input_and_output_rows_are_names_not_fields():
    data = replay(CNPC_VZombie=[
        record("m_iZombieAIType", 0x6678, flags=("SAVE", "KEY"), external="ZombieAIType"),
        record("InputSetZombieAIType", 0, "string", flags=("INPUT",), external="SetZombieAIType"),
        record("m_OnAttackedVictim", 0x66e8, "custom", flags=("SAVE", "KEY", "OUTPUT"),
               external="OnAttackedVictim")])
    zombie = gkb.classify_species("Zombie", "CNPC_VZombie", data, gkb.parse_species_map(MAP))
    assert [r.external for r in zombie.inputfuncs] == ["SetZombieAIType"]
    assert [r.external for r in zombie.outputs] == ["OnAttackedVictim"]
    assert [r.binding for r in zombie.bound] == [("FElysiumNpc", "ZombieAiType")]


def test_input_flag_maps_to_key_and_key_alone_adds_nothing():
    keyed = gkb.Row(cls="C", name="m_x", type="int", offset=1, external="x", kind="field",
                    flags=["SAVE", "KEY"])
    writable = gkb.Row(cls="C", name="m_y", type="int", offset=2, external="y", kind="field",
                       flags=["KEY", "INPUT"])
    assert gkb.flags_of(keyed) == "EElysiumField::Save"
    assert gkb.flags_of(writable) == "EElysiumField::Key"


def test_an_array_word_registers_one_row_per_element():
    data = replay(CNPC_VMingXiao=[record("m_rhProxies", 0x668c, "ehandle", count=6)])
    model = gkb.classify_species("MingXiao", "CNPC_VMingXiao", data, gkb.parse_species_map(MAP))
    assert [r.element for r in model.saved] == [0, 1, 2, 3, 4, 5]
    code = gkb._row_code(model, model.saved[5])
    assert 'TEXT("m_rhProxies[5]")' in code and "E.Proxies[5]" in code
    assert any("std::extent_v<decltype(FElysiumNpcMingXiao::Proxies)> == 6" in line
               for line in gkb._array_asserts(model, model.saved))


def test_a_shadow_row_binds_the_inherited_storage():
    data = replay(CNPC_VTzimisce=[record("m_flIgnoreCollisionTimer", 0x6458, "time")])
    model = gkb.classify_species("Tzimisce", "CNPC_VTzimisce", data, gkb.parse_species_map(MAP))
    assert "&FElysiumNpc::IgnoreCollisionUntil" in gkb._row_code(model, model.saved[0])


def test_an_unanswered_field_and_a_stale_map_row_both_fail():
    data = replay(CNPC_VBach=[record("m_flWarningTime", 0x6694, "time")])
    with pytest.raises(SystemExit, match="has no row in the species shape map"):
        gkb.classify_species("Bach", "CNPC_VBach", data, gkb.parse_species_map(MAP))
    data = replay(CNPC_VBach=[])
    with pytest.raises(SystemExit, match="name no datamap field"):
        gkb.classify_species("Bach", "CNPC_VBach", data, gkb.parse_species_map(MAP))


def test_an_absent_row_is_recorded_not_generated():
    data = replay(CNPC_VBach=[record("m_vecLastOccludeOrigin", 0x6664, "vector")])
    model = gkb.classify_species("Bach", "CNPC_VBach", data, gkb.parse_species_map(MAP))
    assert model.saved == [] and [r.name for r in model.save_unbound] == ["m_vecLastOccludeOrigin"]


@pytest.mark.corpus
def test_the_committed_species_map_answers_every_species_field():
    replay = Path(os.environ.get("ELYSIUM_WORK_ROOT", "")) / "research/ghidra/types/datamap_records-vampire.dll.json"
    if not replay.is_file():
        pytest.skip("the datamap replay is not on this machine")
    data = json.loads(replay.read_text(encoding="utf-8"))
    species = gkb.parse_species_map(REPO.joinpath(*gkb.SPECIES_SHAPE_MAP).read_text(encoding="utf-8"))
    models = {t: gkb.classify_species(gkb.species_binding(t), t, data, species)
              for t in gkb.SPECIES_TABLES}
    # The tutorial rats' keys resolve on the scurrying class, and nowhere else.
    scurrying = {r.external for r in models["CNPC_VScurrying"].bound}
    assert scurrying == {"detection_distance", "ignore_nosferatu", "must_detect",
                         "fright_distance", "fright_duration"}
    assert {r.external for r in models["CNPC_VAnimal"].bound} == {
        "friendship_level", "warn_range", "conflict_range"}
    assert all("detection_distance" not in {r.external for r in m.bound}
               for t, m in models.items() if t != "CNPC_VScurrying")


def test_a_species_save_row_may_not_reuse_a_troika_save_name_on_another_word():
    troika = gkb.ClassModel(name="Npc", tables=("CAI_BaseNPCTroika",))
    troika.saved.append(gkb.Row(kind="save", cls="CAI_BaseNPCTroika", name="m_flIgnoreCollisionTimer",
                                type="time", offset=0x6458, external="", flags=["SAVE"]))
    shadow = replay(CNPC_VTzimisce=[record("m_flIgnoreCollisionTimer", 0x6458, "time")])
    species_map = gkb.parse_species_map(MAP)
    ok = gkb.classify_species("Tzimisce", "CNPC_VTzimisce", shadow, species_map)
    gkb.check_species_save_names([troika, ok], species_map)
    clash = replay(CNPC_VMingXiao=[record("m_flIgnoreCollisionTimer", 0x668c, "ehandle", count=6)])
    bad = gkb.classify_species("MingXiao", "CNPC_VMingXiao", clash, species_map)
    with pytest.raises(SystemExit, match="a Troika save name"):
        gkb.check_species_save_names([troika, bad], species_map)
