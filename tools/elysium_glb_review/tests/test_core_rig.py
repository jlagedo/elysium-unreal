"""Contract tests for the split-rotation bone set.

Two generations of unit answer the same question. The 1.2.0 corpus states the rule per bone
in `mdl.splitRotationBones`; the 1.1.0 corpus states it only as bit `0x2` of the bone flags,
and a reviewer opening either has to see the same pose. Both are read here, together with the
case that decides which wins.
"""

from __future__ import annotations

from core import rig, seams

from . import support


def _payload(**kwargs) -> dict:
    document = support.document_of(support.character_unit("vtmb:character-body:npc/body", **kwargs))
    return seams.root_extension(document, seams.CHARACTER_EXTENSION)


def test_reads_the_bones_a_unit_declares() -> None:
    payload = _payload(
        bones=["Bip01", "Bip01 Pelvis", "Bip01 Spine", "Bip01 Spine1"],
        split_rotation=[
            {
                "bone": 3,
                "name": "Bip01 Spine1",
                "rotation": "model-space",
                "translation": "parent-attached",
            }
        ],
    )
    assert rig.split_rotation_bones(payload) == [rig.SplitBone(index=3, name="Bip01 Spine1")]


def test_leaves_a_declared_entry_that_is_not_model_space_to_ordinary_fk() -> None:
    # The key names the rule rather than implying it, so it can name one this tool does not
    # pose differently.
    payload = _payload(
        bones=["Bip01", "Bip01 Spine1"],
        split_rotation=[{"bone": 1, "name": "Bip01 Spine1", "rotation": "parent-relative"}],
    )
    assert rig.split_rotation_bones(payload) == []


def test_names_a_declared_entry_from_the_bone_table_when_it_carries_no_name() -> None:
    payload = _payload(
        bones=["Bip01", "Bip01 Spine1"],
        split_rotation=[{"bone": 1}],
    )
    assert rig.split_rotation_bones(payload) == [rig.SplitBone(index=1, name="Bip01 Spine1")]


def test_falls_back_to_the_bone_flags_where_a_unit_declares_nothing() -> None:
    payload = _payload(
        bones=["Bip01", "Bip01 Pelvis", "Bip01 Spine", "Bip01 Spine1"],
        bone_flags={"Bip01 Spine1": 0xFFFE, "Bip01 Spine": 0x4},
    )
    assert rig.split_rotation_bones(payload) == [rig.SplitBone(index=3, name="Bip01 Spine1")]


def test_the_declaration_wins_over_the_flags() -> None:
    # A 1.2.0 unit carries both. The flags are the source the exporter read; the key is what
    # it concluded, and a bone it left out is a bone it means to be posed by ordinary FK.
    payload = _payload(
        bones=["Bip01", "Bip01 Spine1"],
        bone_flags={"Bip01 Spine1": 0x2},
        split_rotation=[],
    )
    assert rig.split_rotation_bones(payload) == []


def test_a_unit_with_no_skeleton_names_no_bones() -> None:
    assert rig.split_rotation_bones({}) == []
    assert rig.split_rotation_bones(None) == []
    assert rig.split_rotation_bones({"mdl": {}}) == []
