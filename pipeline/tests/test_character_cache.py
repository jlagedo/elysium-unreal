from __future__ import annotations

import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest

from elysium_pipeline import character_cache as cc, character_partition as cp
from elysium_pipeline.tasking import Manifest


TREE = {"root": "", "spine": "root"}
BANK_A = {"root": "", "spine": "root", "tail": "spine"}
BANK_B = {"root": "", "spine": "root", "tail": "root"}

MANIFEST = {
    "npcs": {
        "amy": {"clips": {"walk": "bank_a", "talk": "amy"}},
        "bob": {"clips": {"walk": "bank_b"}},
    },
    "banks": {"bank_a": {}, "bank_b": {}},
}


class CharacterCacheTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.npc = self.root / "npc"
        (self.npc / "banks").mkdir(parents=True)
        for stem in ("amy", "bob"):
            (self.npc / f"{stem}.eskm").write_bytes(b"body-" + stem.encode())
        for stem in ("bank_a", "bank_b"):
            (self.npc / "banks" / f"{stem}.eskm").write_bytes(b"bank-" + stem.encode())
        (self.npc / "textures.json").write_text("{}", encoding="utf-8")
        (self.npc / "npc_manifest.json").write_text(json.dumps(MANIFEST), encoding="utf-8")

        self.partition = cp.build_partition(
            {"amy": TREE, "bob": TREE}, {"bank_a": BANK_A, "bank_b": BANK_B},
            corpus_fingerprint="corpus-1",
        )
        # Only the paths matter; nothing here reads their contents beyond fingerprinting.
        self.config = SimpleNamespace(repo_root=Path(__file__).resolve().parents[2])
        self.store = Manifest(self.root / "manifest.json")
        self.stems = sorted(self.partition["model_family_of"])

    def tearDown(self):
        self.tmp.cleanup()

    def _plan(self, stems=None, force=False):
        return cc.plan_stages(self.config, self.store, self.npc, MANIFEST, self.partition,
                              stems if stems is not None else self.stems, force=force)

    def _record(self, stale):
        cc.record(self.store, self.config, self.npc, MANIFEST, self.partition, stale)

    def test_a_first_run_is_entirely_stale(self):
        stale = self._plan()
        self.assertTrue(all(stages for stages in stale.values()))
        self.assertIn(cc.GLOBAL_SCOPE, stale)

    def test_after_recording_nothing_is_stale(self):
        self._record(self._plan())
        self.assertEqual({scope: stages for scope, stages in self._plan().items() if stages}, {})

    def test_force_makes_everything_stale_again(self):
        self._record(self._plan())
        self.assertTrue(all(stages for stages in self._plan(force=True).values()))

    def test_a_changed_body_container_invalidates_only_its_family(self):
        self._record(self._plan())
        (self.npc / "amy.eskm").write_bytes(b"body-amy-changed")
        stale = {scope: stages for scope, stages in self._plan().items() if stages}
        family = cp.model_family(self.partition, "amy")
        self.assertEqual(sorted(stale), [f"model.{family}"])

    def test_a_changed_bank_container_invalidates_its_bank_family_and_every_model_skeleton(self):
        self._record(self._plan())
        (self.npc / "banks" / "bank_a.eskm").write_bytes(b"bank-a-changed")
        stale = {scope: stages for scope, stages in self._plan().items() if stages}
        bank_family = cp.bank_family(self.partition, "bank_a")
        self.assertIn("bank_skeletons", stale[f"bank.{bank_family}"])
        # Its own clips go with it: the blend-mask profiles live on the skeleton and are
        # content-addressed by the bones it carries.
        self.assertIn("banks", stale[f"bank.{bank_family}"])
        # Every model family restates its compatibility declaration, but keeps meshes and clips.
        family = cp.model_family(self.partition, "amy")
        self.assertEqual(stale[f"model.{family}"], ["family_skeletons"])

    def test_an_untouched_bank_family_stays_current(self):
        self._record(self._plan())
        (self.npc / "banks" / "bank_a.eskm").write_bytes(b"bank-a-changed")
        stale = self._plan()
        other = cp.bank_family(self.partition, "bank_b")
        self.assertEqual(stale[f"bank.{other}"], [])

    def test_a_changed_family_skeleton_takes_meshes_and_clips_with_it(self):
        stale = {"model.amy": ["family_skeletons"]}
        cc._cascade(stale)
        self.assertEqual(stale["model.amy"], ["family_skeletons", "meshes", "clips"])

    def test_a_changed_mesh_does_not_take_clips(self):
        # The iteration win: a re-textured body costs one mesh and touches no sequence.
        stale = {"model.amy": ["meshes"]}
        cc._cascade(stale)
        self.assertEqual(stale["model.amy"], ["meshes"])

    def test_stages_come_back_in_build_order(self):
        stale = {"model.amy": ["clips", "family_skeletons", "meshes"]}
        cc._cascade(stale)
        self.assertEqual(stale["model.amy"], ["family_skeletons", "meshes", "clips"])

    def test_a_slice_plans_only_the_families_it_names(self):
        self._record(self._plan())
        (self.npc / "amy.eskm").write_bytes(b"changed")
        (self.npc / "bob.eskm").write_bytes(b"changed too")
        # amy and bob share a family here, so name a scope set that excludes nothing real; the
        # assertion is that scopes_for never returns a family no named stem belongs to.
        scopes = cc.scopes_for(self.partition, ["amy"])
        self.assertIn(f"model.{cp.model_family(self.partition, 'amy')}", scopes)
        self.assertEqual(sum(1 for s in scopes if s.startswith("model.")), 1)

    def test_every_declared_bank_family_is_always_in_scope(self):
        # A body declares compatibility with all of them, so a bank skeleton missing because no
        # named model reached it is a body that cannot play a bank it does not yet need.
        scopes = cc.scopes_for(self.partition, ["amy"])
        self.assertEqual(sorted(s for s in scopes if s.startswith("bank.")),
                         sorted(f"bank.{f}" for f in self.partition["banks"]))

    def test_a_focused_plan_names_only_bank_families_the_body_reaches(self):
        scopes = cc.scopes_for(self.partition, ["amy"], manifest=MANIFEST)
        reached = [scope for scope in scopes if scope.startswith("bank.")]
        self.assertEqual(reached, [f"bank.{cp.bank_family(self.partition, 'bank_a')}"])

    def test_stage_inputs_name_real_files(self):
        for scope in cc.scopes_for(self.partition, self.stems):
            for stage in cc.STAGES:
                for path in cc.stage_inputs(self.npc, MANIFEST, self.partition, scope, stage):
                    self.assertTrue(path.is_file(), f"{scope}/{stage} -> {path}")

    def test_prop_only_slice_reaches_no_cast_or_texture_scope(self):
        manifest = {
            **MANIFEST,
            "placed_models": {"switch": {"clips": {"idle": {}}}},
        }
        (self.npc / "placed_models").mkdir()
        (self.npc / "placed_models" / "switch.eskm").write_bytes(b"prop-switch")
        scopes = cc.scopes_for(self.partition, (), ("switch",))
        self.assertEqual(scopes, ["prop.switch"])
        self.assertEqual(
            cc.stage_inputs(self.npc, manifest, self.partition, "prop.switch", "props"),
            [self.npc / "placed_models" / "switch.eskm"],
        )

        stale = cc.plan_stages(
            self.config, self.store, self.npc, manifest, self.partition, (), props=("switch",))
        self.assertEqual(stale, {"prop.switch": ["props"]})
        cc.record(self.store, self.config, self.npc, manifest, self.partition, stale)
        (self.npc / "textures.json").write_text('{"changed":true}', encoding="utf-8")
        current = cc.plan_stages(
            self.config, self.store, self.npc, manifest, self.partition, (), props=("switch",))
        self.assertEqual(current, {"prop.switch": []})


if __name__ == "__main__":
    unittest.main()
