from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

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
    for family in partition["models"]:
        wanted.append(f"Skeletons/{cp.MODEL_SKELETON_PREFIX}{family}")
    for family in partition["banks"]:
        wanted.append(f"Skeletons/{cp.BANK_SKELETON_PREFIX}{family}")
    for stem in partition["model_family_of"]:
        wanted.append(f"Meshes/SK_{stem}")
        wanted.append(f"Materials/MI_SK_{stem}_Body")
        wanted.append(f"Anims/{partition['model_family_of'][stem]}/{stem}/A_idle")
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


class SweepTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.partition = _partition()
        self.npc = _npc_dir(self.root, self.partition)

    def tearDown(self):
        self.tmp.cleanup()

    def test_a_clean_mount_sweeps_nothing(self):
        _mount(self.root, self.partition)
        result = character_sweep.plan(self.root, self.npc, self.partition)
        self.assertEqual(result["orphan_assets"], [])
        self.assertGreater(result["total"], 0)

    def test_finds_a_renamed_family_folder(self):
        # The real failure: a slice partitioned itself, wrote clips under an invented family name,
        # and left the declared family's copy in place. Two answers for one label.
        _mount(self.root, self.partition, extra=("Anims/bob/amy/A_idle",))
        result = character_sweep.plan(self.root, self.npc, self.partition)
        self.assertEqual([p.name for p in result["orphan_assets"]], ["A_idle.uasset"])
        self.assertEqual(result["orphan_dirs"], ["Anims/bob/amy"])

    def test_finds_an_orphan_skeleton(self):
        _mount(self.root, self.partition,
               extra=(f"Skeletons/{cp.MODEL_SKELETON_PREFIX}gone",))
        result = character_sweep.plan(self.root, self.npc, self.partition)
        self.assertEqual([p.stem for p in result["orphan_assets"]],
                         [f"{cp.MODEL_SKELETON_PREFIX}gone"])

    def test_finds_an_orphan_mesh_and_material(self):
        _mount(self.root, self.partition,
               extra=("Meshes/SK_gone", "Materials/MI_SK_gone_Body"))
        result = character_sweep.plan(self.root, self.npc, self.partition)
        self.assertEqual(sorted(p.stem for p in result["orphan_assets"]),
                         ["MI_SK_gone_Body", "SK_gone"])

    def test_keeps_a_material_belonging_to_a_declared_model(self):
        _mount(self.root, self.partition, extra=("Materials/MI_SK_amy_Head",))
        self.assertEqual(character_sweep.plan(self.root, self.npc, self.partition)
                         ["orphan_assets"], [])

    def test_keeps_assets_a_slice_did_not_touch(self):
        # The whole point: the partition covers the cast, so a run that baked only `amy` still
        # knows `bob` owns his own mesh, clips and material.
        _mount(self.root, self.partition)
        self.assertEqual(character_sweep.plan(self.root, self.npc, self.partition)
                         ["orphan_assets"], [])

    def test_apply_removes_and_is_idempotent(self):
        _mount(self.root, self.partition, extra=("Anims/bob/amy/A_idle",))
        first = character_sweep.sweep(self.root, self.npc, self.partition, apply=True)
        self.assertEqual(first["removed"], 1)
        self.assertFalse((self.root / "Characters" / "Anims" / "bob").exists())
        second = character_sweep.sweep(self.root, self.npc, self.partition, apply=True)
        self.assertEqual(second["removed"], 0)

    def test_refuses_to_sweep_most_of_the_mount(self):
        characters = _mount(self.root, self.partition)
        for index in range(40):
            path = characters / "Anims" / "nowhere" / "x" / f"A_{index}.uasset"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"x")
        result = character_sweep.sweep(self.root, self.npc, self.partition, apply=True)
        self.assertEqual(result["removed"], 0)
        self.assertIn("guard", result["refused"])

    def test_force_overrides_the_guard(self):
        characters = _mount(self.root, self.partition)
        for index in range(40):
            path = characters / "Anims" / "nowhere" / "x" / f"A_{index}.uasset"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"x")
        result = character_sweep.sweep(self.root, self.npc, self.partition,
                                       apply=True, force=True)
        self.assertEqual(result["removed"], 40)


class SliceSelectorTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.partition = _partition()
        self.npc = _npc_dir(self.root, self.partition)

    def tearDown(self):
        self.tmp.cleanup()

    def test_no_selector_is_the_whole_cast(self):
        self.assertEqual(resolve_character_slice(self.partition, None, self.npc),
                         ["amy", "bob"])

    def test_a_bare_stem(self):
        self.assertEqual(resolve_character_slice(self.partition, ["amy"], self.npc), ["amy"])

    def test_a_family_selector_takes_every_member(self):
        family = cp.model_family(self.partition, "amy")
        self.assertEqual(resolve_character_slice(self.partition, [f"family:{family}"], self.npc),
                         ["amy", "bob"])

    def test_a_bank_selector_takes_every_model_that_plays_it(self):
        family = cp.bank_family(self.partition, "bank_b")
        self.assertEqual(resolve_character_slice(self.partition, [f"bank:{family}"], self.npc),
                         ["bob"])

    def test_selectors_union_and_deduplicate(self):
        self.assertEqual(
            resolve_character_slice(self.partition, ["amy", "amy", "bob"], self.npc),
            ["amy", "bob"],
        )

    def test_an_unknown_selector_is_refused(self):
        for bad in ("nobody", "family:nobody", "bank:nobody"):
            with self.assertRaises(ValueError):
                resolve_character_slice(self.partition, [bad], self.npc)


if __name__ == "__main__":
    unittest.main()
