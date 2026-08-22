from __future__ import annotations

import json
import os
from pathlib import Path
import struct
import sys
from types import ModuleType, SimpleNamespace
import tempfile
import unittest
from unittest import mock

from elysium_pipeline import bake_cache, character_cache as cc, character_partition as cp
from elysium_pipeline import export_manager, shared_corpus
from elysium_pipeline.exporters import UE_mdl_skeletal
from elysium_pipeline.formats import eskm


BAKE_CHARACTERS_SOURCE = """
VALUE = 1


def log(msg):
    return VALUE


def source_path(stem):
    return stem


def prop_source_path(stem):
    return stem


def cmdline_arg(key, default=""):
    return default


def read_partition():
    return log("partition")


def read_plan():
    return cmdline_arg("BakeCharacterPlan")


def texture_helper():
    return log("tex")


def mesh_helper():
    return log("mesh")


def prop_helper():
    return log("prop")


def bank_helper():
    return log("bank")


def existing_textures():
    return {}


def import_texture_corpus(failed):
    return texture_helper() or existing_textures()


def bake_banks(manifest):
    return bank_helper()


def bake_bodies(manifest):
    return mesh_helper()


def bake_props(manifest):
    return prop_helper()


def main():
    read_partition()
    read_plan()
    bake_banks(None)
    bake_props(None)
    bake_bodies(None)
    existing_textures()
"""

BAKE_MAP_SOURCE = """
def helper():
    return 1


class Bake:
    def __init__(self): pass
    def load_masters(self): return True
    def load_sources(self): return True
    def flush(self): return False
    def checkpoint(self, label): return self.flush()
    def _import_texture_jobs(self, jobs): return {}
    def _material_sets(self): return ()
    def _load_prop_sources(self): return {}
    def _emit(self, stage): return 0
    def stage_textures(self): self._import_texture_jobs([])
    def resolve_textures(self): pass
    def stage_materials(self): self.resolve_textures()
    def resolve_materials(self): self._material_sets()
    def stage_world(self): self.resolve_materials()
    def stage_sky(self): self.resolve_materials()
    def stage_props(self): self._emit("props")
    def stage_level(self): self.resolve_materials()


class CorpusBake(Bake):
    def load_sources(self): return self._load_prop_sources()
    def _material_sets(self): return ()
    def stage_textures(self): self._import_texture_jobs([])
    def resolve_textures(self): pass
    def resolve_materials(self): pass
"""


def skel_section(bones):
    """The `SKEL` payload for [(name, parent index)], with a neutral bind pose."""
    out = struct.pack("<I", len(bones))
    for name, parent in bones:
        out += struct.pack("<I", len(name)) + name.encode("utf-8")
        out += struct.pack("<i", parent)
        out += struct.pack("<3f", 0.0, 0.0, 0.0)
        out += struct.pack("<4f", 0.0, 0.0, 0.0, 1.0)
    return out


def matl_section(rows):
    """The `MATL` payload for [(slot name, albedo uri)], as `UE_mdl_skeletal._matl_section` writes
    it."""
    out = struct.pack("<I", len(rows))
    for name, albedo in rows:
        out += struct.pack("<I", len(name)) + name.encode("utf-8")
        out += struct.pack("<I", len(albedo)) + albedo.encode("utf-8")
    return out


def container(sections):
    """An `.eskm` blob from [(tag, payload)], laid out as `UE_mdl_skeletal._assemble` writes it."""
    header = struct.pack("<4sIII", eskm.MAGIC, eskm.VERSION, len(sections), 0)
    offset = 16 + 20 * len(sections)
    directory = b""
    payload = b""
    for tag, data in sections:
        directory += struct.pack("<4sQQ", tag, offset, len(data))
        offset += len(data)
        payload += data
    return header + directory + payload


BODY_BONES = [("root", -1), ("spine", 0)]
BANK_A_BONES = [("root", -1), ("spine", 0), ("tail", 1)]
BANK_B_BONES = [("root", -1), ("spine", 0), ("tail", 0)]

MANIFEST = {
    "npcs": {
        "amy": {"clips": {"walk": "bank_a", "talk": "amy"}, "own_clips": {"talk": {}}},
        "bob": {"clips": {"walk": "bank_b"}, "own_clips": {"idle": {}}},
    },
    "banks": {"bank_a": {}, "bank_b": {}},
    "placed_models": {"switch": {}},
}


class CharacterFixture(unittest.TestCase):
    """A two-body, two-bank, one-prop cast on disk, with the repo files every policy hashes."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.repo = self.root / "repo"
        self.export = self.root / "export"
        self.npc = self.export / "npc"
        (self.npc / "banks").mkdir(parents=True)
        (self.npc / "placed_models").mkdir(parents=True)

        self.write_body("amy", b"amy-mesh")
        self.write_body("bob", b"bob-mesh")
        self.write_bank("bank_a", BANK_A_BONES, b"bank-a")
        self.write_bank("bank_b", BANK_B_BONES, b"bank-b")
        (self.npc / "placed_models" / "switch.eskm").write_bytes(
            container([(b"SKEL", skel_section([("knob", -1)])), (b"MESH", b"switch")]))
        (self.npc / "textures.json").write_text(
            json.dumps({"textures": {"T_amy": {"uri": "tex/amy.png"}},
                        "bindings": {"amy": {"body": "T_amy"}}}),
            encoding="utf-8")
        (self.npc / "npc_manifest.json").write_text(json.dumps(MANIFEST), encoding="utf-8")

        self.write_repo()
        trees = {
            stem: eskm.bone_parents(eskm.read(self.npc / f"{stem}.eskm"))
            for stem in ("amy", "bob")
        }
        bank_trees = {
            stem: eskm.bone_parents(eskm.read(self.npc / "banks" / f"{stem}.eskm"))
            for stem in ("bank_a", "bank_b")
        }
        self.partition = cp.build_partition(trees, bank_trees, corpus_fingerprint="corpus-1")
        self.config = SimpleNamespace(repo_root=self.repo, export_root=self.export)
        self.stems = sorted(self.partition["model_family_of"])
        self.write_mount()

    def tearDown(self):
        self.tmp.cleanup()

    # ------------------------------------------------------------------ fixture

    def write_body(self, stem, mesh):
        (self.npc / f"{stem}.eskm").write_bytes(
            container([(b"SKEL", skel_section(BODY_BONES)), (b"MESH", mesh),
                       (b"ANIM", stem.encode())]))

    def write_bank(self, stem, bones, payload):
        (self.npc / "banks" / f"{stem}.eskm").write_bytes(
            container([(b"SKEL", skel_section(bones)), (b"ANIM", payload)]))

    def write_repo(self):
        files = {
            "pipeline/unreal/bake_characters.py": BAKE_CHARACTERS_SOURCE,
            "pipeline/unreal/bake_map.py": BAKE_MAP_SOURCE,
            "pipeline/unreal/bake_lib.py": "VALUE = 1\n",
            "pipeline/unreal/make_world_materials.py": "VALUE = 1\n",
            "pipeline/unreal/make_decal_material.py": "VALUE = 1\n",
            "pipeline/unreal/mat_fog.py": "VALUE = 1\n",
            "pipeline/src/elysium_pipeline/asset_names.py": "VALUE = 1\n",
            "pipeline/src/elysium_pipeline/character_partition.py": "VALUE = 1\n",
            "pipeline/src/elysium_pipeline/shared_corpus.py": "VALUE = 1\n",
            "Source/ElysiumUE/Public/ElysiumSkeletalBuild.h": "// build\n",
            "Source/ElysiumUE/Private/Editor/ElysiumSkeletalBuild.cpp": "// build\n",
            "Source/ElysiumUE/Private/Visual/ElysiumSkeletalSource.h": "// source\n",
            "Source/ElysiumUE/Private/Visual/ElysiumSkeletalSource.cpp": "// source\n",
            "Source/ElysiumUE/Private/ElysiumContentPaths.h": "// paths\n",
        }
        for relative, contents in files.items():
            path = self.repo / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding="utf-8")

    def write_mount(self):
        """The packages the single-asset receipts name; a receipt is not current without them."""
        for stage_units in self.plan(force=True).units.values():
            for unit in stage_units.values():
                if not unit.output:
                    continue
                path = Path(unit.output)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"package")

    # ------------------------------------------------------------------- helpers

    def plan(self, stems=None, props=None, force=False):
        return cc.plan(self.config, self.npc, MANIFEST, self.partition,
                       self.stems if stems is None else stems,
                       props=props, force=force)

    def promote(self, planned=None):
        """Promote every planned unit, exactly as a bake that authored them all would."""
        planned = planned if planned is not None else self.plan()
        stages = {}
        for stage, units in planned.units.items():
            if not units:
                continue
            stages[stage] = {
                "policy": planned.policies[stage],
                "assets": {
                    path: {"object_path": path, "fingerprint": unit.fingerprint,
                           "output": unit.output}
                    for path, unit in units.items()
                },
            }
        store = bake_cache.AssetReceiptStore(self.export, cc.SCOPE)
        carried = {
            stage: dict(store.stage_assets(stage))
            for stage in stages
            if store.has_stage(stage, planned.policies[stage])
        }
        for stage, assets in carried.items():
            merged = dict(assets)
            merged.update(stages[stage]["assets"])
            stages[stage]["assets"] = merged
        store.replace_stages(stages)

    def stale(self, planned=None):
        planned = planned if planned is not None else self.plan()
        return {stage: sorted(units) for stage, units in planned.units.items() if units}

    def policies(self):
        return cc.stage_policies(self.config)

    def _edit(self, needle, replacement):
        path = self.repo / "pipeline" / "unreal" / "bake_characters.py"
        path.write_text(BAKE_CHARACTERS_SOURCE.replace(needle, replacement), encoding="utf-8")

    def _report(self, document, stages):
        return {
            "schema": bake_cache.ASSET_RUN_SCHEMA,
            "version": bake_cache.ASSET_SCHEMA_VERSION,
            "run_id": document["run_id"],
            "map": cc.SCOPE,
            "stages": stages,
        }

    def _write_report(self, document, built):
        """The report a bake publishes at a checkpoint.

        `built` is {stage: [object path]} -- the units already saved. Each stage carries in the
        receipts already in the store, exactly as the editor's tracker seeds them, because a
        promoted stage replaces its inventory wholesale.
        """
        store = bake_cache.AssetReceiptStore(self.export, cc.SCOPE)
        stages = {}
        for stage, paths in built.items():
            policy = document["policies"][stage]
            assets = dict(store.stage_assets(stage)) if store.has_stage(stage, policy) else {}
            planned = document["units"][stage]
            for path in paths:
                assets[path] = {"object_path": path,
                                "fingerprint": planned[path]["fingerprint"],
                                "output": planned[path]["output"]}
            stages[stage] = {"policy": policy, "assets": assets,
                             "built": len(paths), "reused": 0, "pruned": 0}
        report = self._report(document, stages)
        bake_cache.write_asset_run_report(self.export, report)
        return report


class CharacterCacheTests(CharacterFixture):
    # --------------------------------------------------------------------- tests

    def test_a_first_plan_is_entirely_stale(self):
        stale = self.stale(self.plan(props=("switch",)))
        self.assertEqual(sorted(stale), sorted(cc.STAGES))

    def test_a_promoted_run_leaves_nothing_stale(self):
        planned = self.plan(props=("switch",))
        self.promote(planned)
        self.assertEqual(self.stale(self.plan(props=("switch",))), {})

    def test_force_makes_every_unit_stale_again(self):
        self.promote()
        self.assertTrue(self.plan(force=True).stale)

    def test_a_changed_body_invalidates_only_that_body(self):
        self.promote()
        self.write_body("amy", b"amy-mesh-changed")
        self.assertEqual(self.stale(), {
            "meshes": [cc.mesh_object_path("amy")],
            "clips": [cc.clips_object_path("amy")],
        })

    def test_a_moved_bone_tree_takes_only_its_own_body(self):
        self.promote()
        (self.npc / "amy.eskm").write_bytes(
            container([(b"SKEL", skel_section(BODY_BONES + [("hair", 1)])),
                       (b"MESH", b"amy-mesh"), (b"ANIM", b"amy")]))
        stale = self.stale()
        self.assertEqual(stale["family_skeletons"],
                         [self.partition["models"]["amy"]["skeleton"]])
        self.assertEqual(stale["meshes"], [cc.mesh_object_path("amy")])
        self.assertEqual(stale["clips"], [cc.clips_object_path("amy")])

    def test_a_moved_bank_restates_every_body_skeleton_and_nothing_else(self):
        self.promote()
        self.write_bank("bank_a", BANK_A_BONES + [("wing", 2)], b"bank-a")
        bank_family = cp.bank_family(self.partition, "bank_a")
        stale = self.stale()
        # Every body's skeleton restates its compatibility declaration; no mesh and no body clip
        # moves.
        self.assertEqual(stale["family_skeletons"],
                         sorted(self.partition["models"][stem]["skeleton"]
                                for stem in ("amy", "bob")))
        self.assertNotIn("meshes", stale)
        self.assertNotIn("clips", stale)
        self.assertEqual(stale["bank_skeletons"],
                         [self.partition["banks"][bank_family]["skeleton"]])
        self.assertEqual(stale["banks"], [cc.bank_clips_object_path("bank_a")])

    def test_an_untouched_bank_keeps_its_clips(self):
        self.promote()
        self.write_bank("bank_a", BANK_A_BONES, b"bank-a-changed")
        self.assertEqual(self.stale(), {"banks": [cc.bank_clips_object_path("bank_a")]})

    def test_a_changed_texture_table_reaches_only_the_bodies_it_binds(self):
        self.promote()
        (self.npc / "textures.json").write_text(
            json.dumps({"textures": {"T_amy": {"uri": "tex/amy_v2.png"}},
                        "bindings": {"amy": {"body": "T_amy"}}}),
            encoding="utf-8")
        stale = self.stale()
        self.assertEqual(stale["textures"], [cc.TEXTURES])
        self.assertEqual(stale["meshes"], [cc.mesh_object_path("amy")])

    def test_a_prop_slice_reaches_no_cast_unit(self):
        planned = self.plan(stems=(), props=("switch",))
        self.assertEqual(self.stale(planned), {"props": [cc.prop_object_path("switch")]})
        self.promote(planned)
        self.assertEqual(self.stale(self.plan(stems=(), props=("switch",))), {})

    def test_a_body_without_own_clips_declares_no_clip_unit(self):
        manifest = json.loads(json.dumps(MANIFEST))
        manifest["npcs"]["bob"]["own_clips"] = {}
        planned = cc.plan(self.config, self.npc, manifest, self.partition, self.stems)
        self.assertNotIn(cc.clips_object_path("bob"), planned.units["clips"])

    def test_a_missing_package_reopens_a_receipted_unit(self):
        self.promote()
        Path(self.plan(force=True).units["meshes"][cc.mesh_object_path("amy")].output).unlink()
        self.assertEqual(self.stale(), {"meshes": [cc.mesh_object_path("amy")]})

    # --------------------------------------------------------------- code policy

    def test_a_stage_policy_moves_only_when_the_code_it_calls_does(self):
        before = self.policies()
        self._edit("def prop_helper():\n    return log(\"prop\")",
                   "def prop_helper():\n    return log(\"prop\") or 2")
        after = self.policies()
        self.assertNotEqual(after["props"], before["props"])
        for stage in ("textures", "meshes", "clips", "family_skeletons", "banks",
                      "bank_skeletons"):
            self.assertEqual(after[stage], before[stage], stage)

    def test_the_body_stages_share_the_loop_that_authors_them(self):
        before = self.policies()
        self._edit("def mesh_helper():\n    return log(\"mesh\")",
                   "def mesh_helper():\n    return log(\"mesh\") or 3")
        after = self.policies()
        for stage in ("family_skeletons", "meshes", "clips"):
            self.assertNotEqual(after[stage], before[stage], stage)
        for stage in ("textures", "props", "banks", "bank_skeletons"):
            self.assertEqual(after[stage], before[stage], stage)

    def test_the_mount_texture_lookup_reaches_the_mesh_stage(self):
        # A mesh binds its albedos through `existing_textures` whenever the texture stage is
        # current and the import is skipped, so the meshes policy is a function of it. The textures
        # policy is too: the import consults the same table to decide what it can skip.
        before = self.policies()
        self._edit("def existing_textures():\n    return {}",
                   "def existing_textures():\n    return {} or log(\"mount\")")
        after = self.policies()
        for stage in ("meshes", "textures"):
            self.assertNotEqual(after[stage], before[stage], stage)
        for stage in ("props", "banks", "bank_skeletons", "family_skeletons", "clips"):
            self.assertEqual(after[stage], before[stage], stage)

    def test_the_run_readers_move_every_stage_policy(self):
        # `cmdline_arg`, `read_partition` and `read_plan` decide which bodies a run bakes, which
        # partition it bakes them into and which units it authors, so no stage may ignore them.
        before = self.policies()
        self._edit("def cmdline_arg(key, default=\"\"):\n    return default",
                   "def cmdline_arg(key, default=\"\"):\n    return default or \"x\"")
        after = self.policies()
        for stage in cc.STAGES:
            self.assertNotEqual(after[stage], before[stage], stage)

    def test_a_character_bake_edit_leaves_the_corpus_policies_alone(self):
        before = {stage: bake_cache.corpus_stage_policy(self.config, stage)
                  for stage in shared_corpus.STAGES}
        self._edit("VALUE = 1", "VALUE = 2")
        after = {stage: bake_cache.corpus_stage_policy(self.config, stage)
                 for stage in shared_corpus.STAGES}
        self.assertEqual(after, before)

    def test_a_map_bake_edit_leaves_the_character_policies_alone(self):
        before = self.policies()
        (self.repo / "pipeline" / "unreal" / "bake_map.py").write_text(
            BAKE_MAP_SOURCE.replace("def stage_world(self): self.resolve_materials()",
                                    "def stage_world(self): self.resolve_materials(); helper()"),
            encoding="utf-8")
        self.assertEqual(self.policies(), before)

    def test_unrelated_code_leaves_every_unit_current(self):
        self.promote()
        (self.repo / "pipeline" / "unreal" / "bake_map.py").write_text(
            BAKE_MAP_SOURCE + "\ndef extra(): pass\n", encoding="utf-8")
        (self.repo / "pipeline" / "unreal" / "make_world_materials.py").write_text(
            "VALUE = 2\n", encoding="utf-8")
        self.assertEqual(self.stale(), {})

    def test_a_policy_change_reopens_every_unit_of_that_stage(self):
        self.promote()
        self._edit("def mesh_helper():\n    return log(\"mesh\")",
                   "def mesh_helper():\n    return log(\"mesh\") or 3")
        stale = self.stale()
        self.assertEqual(stale["meshes"],
                         sorted(cc.mesh_object_path(s) for s in ("amy", "bob")))
        self.assertNotIn("props", stale)

    # ------------------------------------------------------------------ run plan

    def test_the_run_plan_names_only_the_stale_units(self):
        self.promote()
        self.write_body("amy", b"amy-mesh-changed")
        planned = self.plan()
        path, document = cc.write_run_plan(self.npc, planned, force=False)
        self.assertEqual(json.loads(path.read_text(encoding="utf-8")), document)
        self.assertEqual(document["schema"], cc.PLAN_SCHEMA)
        self.assertEqual(document["version"], cc.PLAN_VERSION)
        self.assertEqual(document["batch"], cc.BODY_BATCH)
        self.assertEqual(sorted(document["units"]), ["clips", "meshes"])
        self.assertEqual(list(document["units"]["meshes"]), [cc.mesh_object_path("amy")])
        self.assertEqual(set(document["policies"]), set(cc.STAGES))
        # Every stage the editor is asked to run has a unit in it.
        for scope, stages in document["scopes"].items():
            for stage in stages:
                self.assertTrue(
                    any(unit["scope"] == scope for unit in document["units"][stage].values()),
                    f"{scope}:{stage}")

    def test_a_checkpointed_report_is_salvaged_when_the_bake_dies(self):
        self.promote()
        self.write_body("amy", b"amy-mesh-changed")
        self.write_body("bob", b"bob-mesh-changed")
        planned = self.plan()
        _path, document = cc.write_run_plan(self.npc, planned, force=False)
        self._write_report(document, {"meshes": [cc.mesh_object_path("amy")]})
        self.assertEqual(cc.salvage(self.config, document), 2)
        stale = self.stale()
        self.assertEqual(stale["meshes"], [cc.mesh_object_path("bob")])

    def test_a_report_that_does_not_match_the_plan_is_refused(self):
        self.promote()
        self.write_body("amy", b"amy-mesh-changed")
        planned = self.plan()
        _path, document = cc.write_run_plan(self.npc, planned, force=False)
        report = self._write_report(document, {"meshes": [cc.mesh_object_path("amy")]})

        report["stages"]["meshes"]["policy"] = "another-policy"
        bake_cache.write_asset_run_report(self.export, report)
        with self.assertRaises(RuntimeError):
            cc.load_run_report(self.config, document)

        report["stages"]["meshes"]["policy"] = document["policies"]["meshes"]
        Path(report["stages"]["meshes"]["assets"][cc.mesh_object_path("amy")]["output"]).unlink()
        bake_cache.write_asset_run_report(self.export, report)
        with self.assertRaises(RuntimeError):
            cc.load_run_report(self.config, document)

        report["stages"]["props"] = {"policy": "x", "assets": {}}
        bake_cache.write_asset_run_report(self.export, report)
        with self.assertRaises(RuntimeError):
            cc.load_run_report(self.config, document)

    def test_a_revoked_run_reopens_every_unit_it_planned(self):
        self.promote()
        self.write_body("amy", b"amy-mesh-changed")
        planned = self.plan()
        _path, document = cc.write_run_plan(self.npc, planned, force=False)
        self._write_report(document, {
            "meshes": [cc.mesh_object_path("amy")],
            "clips": [cc.clips_object_path("amy")],
        })
        cc.promote_run(self.config, cc.load_run_report(self.config, document))
        self.assertEqual(self.stale(), {})
        self.assertEqual(cc.revoke(self.config, document), 2)
        self.assertEqual(self.stale(), {
            "meshes": [cc.mesh_object_path("amy")],
            "clips": [cc.clips_object_path("amy")],
        })


class CharacterBakeOrchestrationTests(CharacterFixture):
    """What `_run_character_bake` leaves in the receipt store for each way a run can end.

    The editor checkpoints into the LIVE store while it bakes, so every path out of the
    orchestrator has to state whether those receipts stand.
    """

    def _run(self):
        """A planned run over one changed body: (plan path, plan document, planned unit paths)."""
        self.promote()
        self.write_body("amy", b"amy-mesh-changed")
        planned = self.plan()
        path, document = cc.write_run_plan(self.npc, planned, force=False)
        return path, document, {stage: sorted(units) for stage, units in document["units"].items()}

    def _checkpointed(self, document):
        """Everything the editor's batch checkpoints publish once every planned unit has landed."""
        report = self._write_report(
            document, {stage: list(units) for stage, units in document["units"].items()})
        bake_cache.checkpoint_receipts(self.export, cc.SCOPE, {
            stage: {"policy": stage_report["policy"], "assets": stage_report["assets"]}
            for stage, stage_report in report["stages"].items()})
        return report

    def _bake(self, config, runner, stems, props=(), plan=None):
        del config, runner, stems, props, plan
        self._checkpointed(self.document)

    def test_a_verified_run_promotes_and_revokes_nothing(self):
        plan_path, self.document, planned = self._run()
        with (
            mock.patch.object(export_manager.unreal, "bake_characters", side_effect=self._bake),
            mock.patch.object(export_manager.unreal, "verify_characters"),
        ):
            export_manager._run_character_bake(
                self.config, object(), self.document, plan_path, self.stems)
        self.assertEqual(self.stale(), {})
        self.assertTrue(planned)

    def test_a_failed_sweep_revokes_what_the_checkpoints_made_current(self):
        plan_path, self.document, planned = self._run()

        def sweep():
            raise export_manager.ExportBakeFailure("character sweep refused: orphan")

        with (
            mock.patch.object(export_manager.unreal, "bake_characters", side_effect=self._bake),
            mock.patch.object(export_manager.unreal, "verify_characters") as verify,
        ):
            with self.assertRaises(export_manager.ExportBakeFailure):
                export_manager._run_character_bake(
                    self.config, object(), self.document, plan_path, self.stems,
                    after_bake=sweep)
            verify.assert_not_called()
        self.assertEqual(self.stale(), planned)

    def test_an_invalid_report_revokes_what_the_checkpoints_made_current(self):
        plan_path, self.document, planned = self._run()

        def bake(config, runner, stems, props=(), plan=None):
            report = self._bake(config, runner, stems, props=props, plan=plan)
            del report
            broken = json.loads(
                cc.report_path(self.config, self.document["run_id"]).read_text(encoding="utf-8"))
            broken["stages"]["meshes"]["policy"] = "another-policy"
            bake_cache.write_asset_run_report(self.export, broken)

        with (
            mock.patch.object(export_manager.unreal, "bake_characters", side_effect=bake),
            mock.patch.object(export_manager.unreal, "verify_characters") as verify,
        ):
            with self.assertRaises(RuntimeError):
                export_manager._run_character_bake(
                    self.config, object(), self.document, plan_path, self.stems)
            verify.assert_not_called()
        self.assertEqual(self.stale(), planned)

    def test_a_verify_that_fails_for_any_reason_revokes(self):
        # Not just `UnrealFailure`: a launcher that cannot spawn the process, or a reader that
        # cannot open its output, leaves the same unverified mount behind.
        for index, error in enumerate((
                OSError("verifier could not start"),
                ValueError("verifier report is not json"),
                export_manager.unreal.UnrealFailure("verify refused the mount"))):
            with self.subTest(error=type(error).__name__):
                if index:
                    self.tearDown()
                    self.setUp()
                plan_path, self.document, planned = self._run()
                with (
                    mock.patch.object(export_manager.unreal, "bake_characters",
                                      side_effect=self._bake),
                    mock.patch.object(export_manager.unreal, "verify_characters",
                                      side_effect=error),
                ):
                    with self.assertRaises(type(error)):
                        export_manager._run_character_bake(
                            self.config, object(), self.document, plan_path, self.stems)
                self.assertEqual(self.stale(), planned)

    def test_a_dead_editor_keeps_the_units_it_checkpointed(self):
        self.promote()
        self.write_body("amy", b"amy-mesh-changed")
        self.write_body("bob", b"bob-mesh-changed")
        planned = self.plan()
        plan_path, document = cc.write_run_plan(self.npc, planned, force=False)

        def bake(config, runner, stems, props=(), plan=None):
            del config, runner, stems, props, plan
            self._write_report(document, {"meshes": [cc.mesh_object_path("amy")]})
            raise export_manager.unreal.UnrealFailure("editor died")

        with (
            mock.patch.object(export_manager.unreal, "bake_characters", side_effect=bake),
            mock.patch.object(export_manager.unreal, "verify_characters") as verify,
        ):
            with self.assertRaises(export_manager.unreal.UnrealFailure):
                export_manager._run_character_bake(
                    self.config, object(), document, plan_path, self.stems)
            verify.assert_not_called()
        # The salvaged mesh stands; only the body the editor never reached reopens.
        self.assertEqual(self.stale()["meshes"], [cc.mesh_object_path("bob")])


REPO_ROOT = Path(__file__).resolve().parents[2]
WORKER = REPO_ROOT / "pipeline" / "unreal" / "bake_characters.py"


class _FakeAsset:
    def __init__(self, name, package):
        self._name = name
        self._package = package

    def get_name(self):
        return self._name

    def get_path_name(self):
        return "%s.%s" % (self._package, self._name)


class _FakeEditorAssetLibrary:
    """The subset of `unreal.EditorAssetLibrary` the worker's mount lookup calls."""

    def __init__(self):
        self.assets = {}

    def add(self, folder, name):
        package = "%s/%s" % (folder, name)
        self.assets[package] = _FakeAsset(name, package)
        return self.assets[package]

    def list_assets(self, folder, recursive=False, include_folder=False):
        del recursive, include_folder
        return sorted(path for path in self.assets if path.rsplit("/", 1)[0] == folder)

    def load_asset(self, path):
        return self.assets.get(path)


class _FakeBakeLib(ModuleType):
    """`pipeline.unreal.bake_lib` for a headless test: records imports, saves into the mount."""

    def __init__(self, editor, *, unimportable=(), unsavable=()):
        super().__init__("pipeline.unreal.bake_lib")
        self.editor = editor
        self.unimportable = set(unimportable)
        self.unsavable = set(unsavable)
        self.imported = []
        self.saved = []

    def ensure_dir(self, package):
        return package

    def import_textures(self, jobs, package):
        out = {}
        for source, name in jobs:
            self.imported.append((source, name))
            if name not in self.unimportable:
                out[name] = self.editor.add(package, name)
        return out

    def configure_texture(self, texture, role):
        del texture, role

    def save(self, asset_path):
        self.saved.append(asset_path)
        return asset_path.rsplit("/", 1)[-1] not in self.unsavable


class _FakeLibrary:
    """`unreal.ElysiumSkeletalBuildLibrary`, recording what the worker asked it to build."""

    def __init__(self, bones):
        self.bones = bones
        self.skeletons = []
        self.meshes = []
        self.sequences = []
        self.declared = []

    def build_family_skeleton(self, sources, package, merge):
        del sources, merge
        self.skeletons.append(package)
        return "", self.bones

    def build_skeletal_mesh_from_source(self, source, package, skeleton, master, materials,
                                        bindings, eyes):
        del source, skeleton, master, materials, bindings, eyes
        self.meshes.append(package)
        return ""

    def build_anim_sequences_from_source(self, source, package, skeleton):
        del source, skeleton
        self.sequences.append(package)
        return "", 1, 0

    def build_blend_spaces_from_grids(self, blends, package, skeleton):
        del blends, package, skeleton
        return "", 0, 0, 0

    def declare_compatible_skeletons(self, package, banks):
        del banks
        self.declared.append(package)
        return ""


class CharacterBakeWorkerTests(CharacterFixture):
    """The editor worker's own control flow, executed under stubs for `unreal` and `bake_lib`.

    The real `pipeline/unreal/bake_characters.py` source runs here -- everything below the module's
    trailing `main()` call -- so what is asserted is the shipped control flow rather than a copy.
    """

    def setUp(self):
        super().setUp()
        # Give both bodies a material table and the albedos it names, so the mesh path has real
        # bindings to resolve and the texture table has more than one row to cover.
        for stem in ("amy", "bob"):
            (self.npc / f"{stem}.eskm").write_bytes(container([
                (b"SKEL", skel_section(BODY_BONES)),
                (b"MATL", matl_section([("body", f"tex/{stem}.png")])),
                (b"MESH", f"{stem}-mesh".encode()),
                (b"ANIM", stem.encode())]))
            (self.npc / "tex").mkdir(exist_ok=True)
            (self.npc / "tex" / f"{stem}.png").write_bytes(b"png")
        (self.npc / "textures.json").write_text(
            json.dumps({"textures": {"T_amy": {"uri": "tex/amy.png"},
                                     "T_bob": {"uri": "tex/bob.png"}},
                        "bindings": {"amy": {"body": "T_amy"},
                                     "bob": {"body": "T_bob"}}}),
            encoding="utf-8")
        self.editor = _FakeEditorAssetLibrary()
        self.bl = _FakeBakeLib(self.editor)
        self.worker = self._load_worker()

    def _load_worker(self):
        source = WORKER.read_text(encoding="utf-8")
        marker = "\nmain()\n"
        self.assertTrue(source.endswith(marker), f"{WORKER} must end by calling main()")
        unreal_stub = ModuleType("unreal")
        unreal_stub.log = lambda msg: None
        unreal_stub.log_error = lambda msg: None
        unreal_stub.EditorAssetLibrary = self.editor
        unreal_stub.SystemLibrary = SimpleNamespace(get_command_line=lambda: "")
        unreal_stub.AssetRegistryHelpers = SimpleNamespace(
            get_asset_registry=lambda: SimpleNamespace(
                scan_paths_synchronous=lambda *args, **kwargs: None))
        unreal_stub.ElysiumSkeletalBuildLibrary = SimpleNamespace(
            release_baked_packages=lambda package: 0)
        module = ModuleType("bake_characters_under_test")
        module.__file__ = str(WORKER)
        with (
            mock.patch.dict(sys.modules, {"unreal": unreal_stub,
                                          "pipeline.unreal.bake_lib": self.bl}),
            mock.patch.dict(os.environ, {"ELYSIUM_EXPORT_ROOT": str(self.export)}),
        ):
            exec(compile(source[:-len(marker)], module.__file__, "exec"), module.__dict__)
        return module

    def _document(self):
        _path, document = cc.write_run_plan(self.npc, self.plan(force=True), force=True)
        return document

    # ------------------------------------------------------ the texture unit's coverage

    def test_a_sliced_run_imports_every_texture_the_table_names(self):
        # The unit's recipe is the whole document, so its coverage has to be too: an import driven
        # by the run's own bodies would receipt the unit with another body's albedo still absent,
        # and every later run would skip the import and build that body untextured.
        failed = []
        table = self.worker.import_texture_corpus(failed)
        self.assertEqual(failed, [])
        self.assertEqual(sorted(table), ["T_amy", "T_bob"])
        self.assertEqual(sorted(name for _source, name in self.bl.imported), ["T_amy", "T_bob"])

    def test_a_texture_already_on_the_mount_is_returned_without_reimporting(self):
        self.editor.add(self.worker.TEXTURES, "T_amy")
        failed = []
        table = self.worker.import_texture_corpus(failed)
        self.assertEqual(failed, [])
        self.assertEqual(sorted(table), ["T_amy", "T_bob"])
        self.assertEqual([name for _source, name in self.bl.imported], ["T_bob"])

    def test_a_forced_run_reimports_what_is_already_on_the_mount(self):
        # `--force` is the recovery surface: an asset on the mount is exactly what it exists to
        # replace, so presence stops being a reason to skip.
        self.editor.add(self.worker.TEXTURES, "T_amy")
        failed = []
        self.worker.import_texture_corpus(failed, force=True)
        self.assertEqual(failed, [])
        self.assertEqual(sorted(name for _source, name in self.bl.imported), ["T_amy", "T_bob"])

    def test_a_missing_albedo_fails_the_texture_unit(self):
        (self.npc / "tex" / "bob.png").unlink()
        failed = []
        table = self.worker.import_texture_corpus(failed)
        self.assertEqual(failed, ["bob.png"])
        self.assertEqual(sorted(table), ["T_amy"])

    def test_a_texture_that_did_not_save_fails_the_texture_unit(self):
        self.bl.unsavable = {"T_bob"}
        failed = []
        table = self.worker.import_texture_corpus(failed)
        self.assertEqual(failed, ["T_bob"])
        self.assertEqual(sorted(table), ["T_amy"])

    # ------------------------------------------------------ a failed unit is not receipted

    def test_material_bindings_reports_a_slot_it_cannot_resolve(self):
        blob = eskm.read(self.npc / "amy.eskm")
        bindings, unbound = self.worker.material_bindings(blob, {})
        self.assertEqual(bindings, {})
        self.assertEqual(unbound, ["T_amy"])

    def test_a_body_whose_albedo_is_absent_is_not_built_or_receipted(self):
        document = self._document()
        tracker = self.worker.CharacterTracker(document)
        library = _FakeLibrary(bones=2)
        failed = []
        self.worker.bake_bodies(MANIFEST, self.partition, ["amy"], library, failed, document,
                                tracker, {}, [], ["amy"])
        self.assertIn("amy", failed)
        self.assertEqual(library.meshes, [])
        self.assertEqual(tracker.stages["meshes"]["assets"], {})
        self.assertEqual(tracker.stages["clips"]["assets"], {})
        self.assertEqual(tracker.stages["family_skeletons"]["assets"], {})

    def test_a_bank_skeleton_the_partition_disagrees_with_bakes_no_sequence(self):
        document = self._document()
        tracker = self.worker.CharacterTracker(document)
        library = _FakeLibrary(bones=99)
        failed = []
        self.worker.bake_banks(MANIFEST, self.partition, self.stems, library, failed, document,
                               tracker)
        self.assertTrue(failed)
        self.assertEqual(library.sequences, [])
        self.assertEqual(tracker.stages["bank_skeletons"]["assets"], {})
        self.assertEqual(tracker.stages["banks"]["assets"], {})

    def test_a_body_skeleton_the_partition_disagrees_with_bakes_no_body(self):
        document = self._document()
        tracker = self.worker.CharacterTracker(document)
        library = _FakeLibrary(bones=99)
        failed = []
        textures = {"T_amy": _FakeAsset("T_amy", self.worker.TEXTURES + "/T_amy"),
                    "T_bob": _FakeAsset("T_bob", self.worker.TEXTURES + "/T_bob")}
        self.worker.bake_bodies(MANIFEST, self.partition, self.stems, library, failed, document,
                                tracker, textures, [], self.stems)
        self.assertTrue(failed)
        self.assertEqual(library.meshes, [])
        self.assertEqual(library.sequences, [])
        self.assertEqual(tracker.stages["family_skeletons"]["assets"], {})
        self.assertEqual(tracker.stages["meshes"]["assets"], {})
        self.assertEqual(tracker.stages["clips"]["assets"], {})

    def test_sound_bodies_still_bake_and_receipt_themselves(self):
        document = self._document()
        tracker = self.worker.CharacterTracker(document)
        library = _FakeLibrary(bones=2)
        failed = []
        textures = {"T_amy": _FakeAsset("T_amy", self.worker.TEXTURES + "/T_amy"),
                    "T_bob": _FakeAsset("T_bob", self.worker.TEXTURES + "/T_bob")}
        self.worker.bake_bodies(MANIFEST, self.partition, self.stems, library, failed, document,
                                tracker, textures, [], self.stems)
        self.assertEqual(failed, [])
        self.assertEqual(sorted(tracker.stages["meshes"]["assets"]),
                         sorted(cc.mesh_object_path(stem) for stem in self.stems))
        self.assertTrue(tracker.stages["family_skeletons"]["assets"])


class WriteIfUnchangedTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_partition_writer_skips_a_byte_identical_rewrite(self):
        path = self.root / "families.json"
        export_manager._write_json(path, {"a": 1, "b": [2, 3]})
        stamp = path.stat().st_mtime_ns
        export_manager._write_json(path, {"b": [2, 3], "a": 1})
        self.assertEqual(path.stat().st_mtime_ns, stamp)
        export_manager._write_json(path, {"a": 2})
        self.assertNotEqual(path.stat().st_mtime_ns, stamp)

    def test_the_container_writer_skips_a_byte_identical_rewrite(self):
        path = self.root / "amy.eskm"
        UE_mdl_skeletal._write_container(str(path), b"payload")
        stamp = path.stat().st_mtime_ns
        UE_mdl_skeletal._write_container(str(path), b"payload")
        self.assertEqual(path.stat().st_mtime_ns, stamp)
        UE_mdl_skeletal._write_container(str(path), b"payload-2")
        self.assertEqual(path.read_bytes(), b"payload-2")


if __name__ == "__main__":
    unittest.main()
