from __future__ import annotations

import json
from pathlib import Path
import tempfile
from unittest import mock

import pytest

from elysium_pipeline import character_partition as cp, character_sweep
from elysium_pipeline.export_manager import resolve_character_slice

TREE = {"root": "", "spine": "root"}
# Disagrees with BANK_A about tail's parent, so the two banks cannot share a skeleton and the
# partition splits them -- which is what makes `bank:` a meaningful selector to test.
BANK_A = {"root": "", "spine": "root", "tail": "spine"}
BANK_B = {"root": "", "spine": "root", "tail": "root"}


def _partition():
    return cp.build_partition({"amy": TREE, "bob": TREE}, {"bank_a": BANK_A, "bank_b": BANK_B})


MANIFEST = {
    "npcs": {
        "amy": {"clips": {"walk": "bank_a", "talk": "amy"}},
        "bob": {"clips": {"walk": "bank_b"}},
    },
    "banks": {"bank_a": {}, "bank_b": {}},
}


def _mount(root: Path, partition: dict, *, extra=()):
    """A mount carrying exactly what `partition` declares, plus `extra` relative asset paths."""
    characters = root / "Characters"
    wanted = []
    for stem in partition["models"]:
        wanted.append(f"Skeletons/{cp.MODEL_SKELETON_PREFIX}{stem}")
    for family in partition["banks"]:
        wanted.append(f"Skeletons/{cp.BANK_SKELETON_PREFIX}{family}")
    for stem in partition["model_family_of"]:
        wanted.append(f"Meshes/SK_{stem}")
        wanted.append(f"Materials/MI_SK_{stem}_Body")
        wanted.append(f"Anims/{stem}/A_idle")
    for bank in partition["bank_family_of"]:
        wanted.append(f"Anims/_banks/{bank}/A_walk")
    wanted.append("Textures/T_skin")
    for rel in list(wanted) + list(extra):
        path = characters / (rel + ".uasset")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"x")
    return characters


def _npc_dir(root: Path, partition: dict):
    npc = root / "npc"
    npc.mkdir(parents=True, exist_ok=True)
    (npc / "npc_manifest.json").write_text(json.dumps(MANIFEST), encoding="utf-8")
    (npc / "textures.json").write_text(
        json.dumps({"textures": {"T_skin": {"uri": "tex/skin.png", "used_by": ["amy"]}}}),
        encoding="utf-8",
    )
    (npc / "families.json").write_text(json.dumps(partition), encoding="utf-8")
    return npc


@pytest.fixture
def partition():
    return _partition()


@pytest.fixture
def npc(tmp_path, partition):
    return _npc_dir(tmp_path, partition)


def test_a_clean_mount_sweeps_nothing(tmp_path, partition, npc):
    _mount(tmp_path, partition)
    result = character_sweep.plan(tmp_path, npc, partition)
    assert result["orphan_assets"] == []
    assert result["total"] > 0


def test_finds_a_stale_two_level_anim_folder(tmp_path, partition, npc):
    # Residue from the retired rig-family layout: a two-level `Anims/<family>/<owner>` path is
    # never expected under the flat `Anims/<stem>` layout, whatever name the first segment
    # carries.
    _mount(tmp_path, partition, extra=("Anims/gone/amy/A_idle",))
    result = character_sweep.plan(tmp_path, npc, partition)
    assert [p.name for p in result["orphan_assets"]] == ["A_idle.uasset"]
    assert result["orphan_dirs"] == ["Anims/gone/amy"]


def test_finds_an_orphan_mesh_and_material(tmp_path, partition, npc):
    _mount(tmp_path, partition,
           extra=("Meshes/SK_gone", "Materials/MI_SK_gone_Body"))
    result = character_sweep.plan(tmp_path, npc, partition)
    assert sorted(p.stem for p in result["orphan_assets"]) == ["MI_SK_gone_Body", "SK_gone"]


def test_keeps_a_material_belonging_to_a_declared_model(tmp_path, partition, npc):
    _mount(tmp_path, partition, extra=("Materials/MI_SK_amy_Head",))
    result = character_sweep.plan(tmp_path, npc, partition)
    assert result["orphan_assets"] == []


def test_apply_removes_and_is_idempotent(tmp_path, partition, npc):
    # Under flat `Anims/<stem>`, `Anims/bob` is a legitimate body folder -- the orphan has to
    # sit under a parent no declared stem owns, or the prune assertion below would fail on a
    # folder the partition still wants.
    _mount(tmp_path, partition, extra=("Anims/gone/amy/A_idle",))
    first = character_sweep.sweep(tmp_path, npc, partition, apply=True)
    assert first["removed"] == 1
    assert not (tmp_path / "Characters" / "Anims" / "gone").exists()
    second = character_sweep.sweep(tmp_path, npc, partition, apply=True)
    assert second["removed"] == 0


def test_refuses_to_sweep_most_of_the_mount(tmp_path, partition, npc):
    characters = _mount(tmp_path, partition)
    for index in range(40):
        path = characters / "Anims" / "nowhere" / "x" / f"A_{index}.uasset"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"x")
    result = character_sweep.sweep(tmp_path, npc, partition, apply=True)
    assert result["removed"] == 0
    assert "guard" in result["refused"]


def test_force_overrides_the_guard(tmp_path, partition, npc):
    characters = _mount(tmp_path, partition)
    for index in range(40):
        path = characters / "Anims" / "nowhere" / "x" / f"A_{index}.uasset"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"x")
    result = character_sweep.sweep(tmp_path, npc, partition,
                                   apply=True, force=True)
    assert result["removed"] == 40


def test_no_selector_is_the_whole_cast(tmp_path, partition, npc):
    assert resolve_character_slice(partition, None, npc) == ["amy", "bob"]


def test_a_bare_stem(tmp_path, partition, npc):
    assert resolve_character_slice(partition, ["amy"], npc) == ["amy"]


def test_a_family_selector_is_a_stem_alias(tmp_path, partition, npc):
    # `family:<name>` survives as a one-member alias -- a model is its own family now, so it
    # resolves to exactly the named stem, never a wider group.
    family = cp.model_family(partition, "amy")
    assert resolve_character_slice(partition, [f"family:{family}"], npc) == ["amy"]


def test_a_bank_selector_takes_every_model_that_plays_it(tmp_path, partition, npc):
    family = cp.bank_family(partition, "bank_b")
    assert resolve_character_slice(partition, [f"bank:{family}"], npc) == ["bob"]


def test_selectors_union_and_deduplicate(tmp_path, partition, npc):
    assert resolve_character_slice(partition, ["amy", "amy", "bob"], npc) == ["amy", "bob"]


def test_an_unknown_selector_is_refused(tmp_path, partition, npc):
    for bad in ("nobody", "family:nobody", "bank:nobody"):
        with pytest.raises(ValueError):
            resolve_character_slice(partition, [bad], npc)
