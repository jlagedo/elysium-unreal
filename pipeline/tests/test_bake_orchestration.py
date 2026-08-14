from __future__ import annotations

import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from elysium_pipeline import export_manager
from elysium_pipeline.placed_models import PlacedModelUse
from elysium_pipeline.tasking import Manifest, TaskResult


class BakeOrchestrationTests(unittest.TestCase):
    def _config(self, temporary: str):
        repo = Path(temporary) / "repo"
        export = Path(temporary) / "exports"
        baked = repo / "Plugins" / "ElysiumBaked" / "Content" / "test_map"
        baked.mkdir(parents=True)
        (baked / "test_map.umap").write_bytes(b"level")
        export.mkdir(parents=True)
        return SimpleNamespace(repo_root=repo, export_root=export)

    def _record_verification(self, config) -> None:
        task = export_manager._verification_task(config, "test_map")
        Manifest(config.export_root / ".elysium-manifest.json").record(
            TaskResult(
                name=task.name,
                status="ok",
                duration_seconds=0.0,
                fingerprint=task.fingerprint(),
                outputs=[str(path) for path in task.outputs],
            )
        )

    def test_noop_plan_launches_no_unreal_process(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            self._record_verification(config)
            with (
                mock.patch.object(export_manager.bake_cache, "plan_stages", return_value={}),
                mock.patch.object(export_manager.unreal, "bake_maps") as bake,
                mock.patch.object(export_manager.unreal, "verify_bakes") as verify,
            ):
                export_manager.bake_and_verify(config, object(), ["test_map"])
            bake.assert_not_called()
            verify.assert_not_called()

    def test_particle_plan_requests_only_particle_stage(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            plan_path = Path(temporary) / "plan.json"
            plan_document = {
                "run_id": "test-run",
                "maps": {"test_map": {
                    "stages": ["particles"],
                    "fingerprints": {"particles": "frozen"},
                    "policies": {"particles": "policy"},
                }},
            }
            reports = {"test_map": {"stages": {
                "particles": {"built": 1, "pruned": 0},
            }}}
            order: list[str] = []
            with (
                mock.patch.object(
                    export_manager.bake_cache,
                    "plan_stages",
                    return_value={"test_map": ("particles",)},
                ),
                mock.patch.object(
                    export_manager.bake_cache,
                    "create_asset_run_plan",
                    return_value=(plan_path, plan_document),
                ),
                mock.patch.object(
                    export_manager.bake_cache,
                    "load_asset_run_reports",
                    return_value=reports,
                ),
                mock.patch.object(
                    export_manager.bake_cache,
                    "mutated_maps",
                    return_value={"test_map"},
                ),
                mock.patch.object(
                    export_manager.bake_cache, "assert_asset_run_inputs_current"
                ) as assert_current,
                mock.patch.object(
                    export_manager.bake_cache, "promote_asset_run",
                    side_effect=lambda *_a, **_k: order.append("promote")) as promote,
                mock.patch.object(
                    export_manager.bake_cache, "record_stages",
                    side_effect=lambda *_a, **_k: order.append("record")) as record,
                mock.patch.object(export_manager.unreal, "bake_maps") as bake,
                mock.patch.object(
                    export_manager.unreal, "verify_bakes",
                    side_effect=lambda *_a, **_k: order.append("verify")) as verify,
            ):
                export_manager.bake_and_verify(config, object(), ["test_map"])
            bake.assert_called_once_with(
                config,
                mock.ANY,
                ["test_map"],
                stages="particles",
                asset_plan=plan_path,
            )
            verify.assert_called_once_with(config, mock.ANY, ["test_map"])
            promote.assert_called_once_with(config, plan_document, reports)
            record.assert_called_once()
            self.assertEqual(assert_current.call_count, 2)
            # A validated report is bake truth, so the receipts advance before verification runs.
            self.assertEqual(order, ["promote", "record", "verify"])

    def test_unverified_run_bakes_and_promotes_without_verify_bakes(self) -> None:
        # A focused map iteration trusts a clean bake commandlet exit; the deep verification
        # commandlet stays a separate, explicitly-requested acceptance check.
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            plan_path = Path(temporary) / "plan.json"
            plan_document = {
                "run_id": "test-run",
                "maps": {"test_map": {
                    "stages": ["particles"],
                    "fingerprints": {"particles": "frozen"},
                    "policies": {"particles": "policy"},
                }},
            }
            reports = {"test_map": {"stages": {
                "particles": {"built": 1, "pruned": 0},
            }}}
            with (
                mock.patch.object(
                    export_manager.bake_cache,
                    "plan_stages",
                    return_value={"test_map": ("particles",)},
                ),
                mock.patch.object(
                    export_manager.bake_cache,
                    "create_asset_run_plan",
                    return_value=(plan_path, plan_document),
                ),
                mock.patch.object(
                    export_manager.bake_cache,
                    "load_asset_run_reports",
                    return_value=reports,
                ),
                mock.patch.object(
                    export_manager.bake_cache, "assert_asset_run_inputs_current"
                ) as assert_current,
                mock.patch.object(
                    export_manager.bake_cache, "promote_asset_run"
                ) as promote,
                mock.patch.object(
                    export_manager.bake_cache, "record_stages"
                ) as record,
                mock.patch.object(export_manager.unreal, "bake_maps") as bake,
                mock.patch.object(export_manager.unreal, "verify_bakes") as verify,
            ):
                export_manager.bake_and_verify(
                    config, object(), ["test_map"], verify=False
                )
            bake.assert_called_once()
            promote.assert_called_once_with(config, plan_document, reports)
            record.assert_called_once()
            verify.assert_not_called()
            self.assertEqual(assert_current.call_count, 1)

            task = export_manager._verification_task(config, "test_map")
            reloaded = Manifest(config.export_root / ".elysium-manifest.json")
            self.assertFalse(reloaded.can_skip(task, task.fingerprint()))

    def test_failed_bake_does_not_record_stage_receipts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            with (
                mock.patch.object(
                    export_manager.bake_cache,
                    "plan_stages",
                    return_value={"test_map": ("particles",)},
                ),
                mock.patch.object(export_manager.bake_cache, "record_stages") as record,
                mock.patch.object(
                    export_manager.unreal,
                    "bake_maps",
                    side_effect=RuntimeError("synthetic failure"),
                ),
                mock.patch.object(export_manager.unreal, "verify_bakes") as verify,
            ):
                with self.assertRaises(export_manager.ExportBakeFailure):
                    export_manager.bake_and_verify(config, object(), ["test_map"])
            record.assert_not_called()
            verify.assert_not_called()

    def test_a_failed_batch_salvages_the_maps_whose_reports_validate(self) -> None:
        # A per-map report is written once that map's stages completed and its packages were
        # saved, so a later map's failure does not throw away the work that already landed.
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            plan_path = Path(temporary) / "plan.json"
            plan_document = {
                "run_id": "test-run",
                "maps": {
                    "map_a": {
                        "stages": ["particles"],
                        "fingerprints": {"particles": "frozen-a"},
                        "policies": {"particles": "policy"},
                    },
                    "map_b": {
                        "stages": ["particles"],
                        "fingerprints": {"particles": "frozen-b"},
                        "policies": {"particles": "policy"},
                    },
                },
            }
            reports = {"map_a": {"stages": {"particles": {"built": 1, "pruned": 0}}}}

            def load(_config, document):
                if set(document["maps"]) != {"map_a"}:
                    raise RuntimeError("asset bake report missing or invalid")
                return reports

            with (
                mock.patch.object(
                    export_manager.bake_cache, "plan_stages",
                    return_value={"map_a": ("particles",), "map_b": ("particles",)}),
                mock.patch.object(
                    export_manager.bake_cache, "create_asset_run_plan",
                    return_value=(plan_path, plan_document)),
                mock.patch.object(
                    export_manager.bake_cache, "load_asset_run_reports", side_effect=load),
                mock.patch.object(export_manager.bake_cache, "promote_asset_run") as promote,
                mock.patch.object(export_manager.bake_cache, "record_stages") as record,
                mock.patch.object(
                    export_manager.unreal, "bake_maps",
                    side_effect=RuntimeError("synthetic batch failure")),
                mock.patch.object(export_manager.unreal, "verify_bakes") as verify,
            ):
                with self.assertRaises(export_manager.ExportBakeFailure):
                    export_manager.bake_and_verify(config, object(), ["map_a", "map_b"])
            verify.assert_not_called()
            promote.assert_called_once()
            self.assertEqual(set(promote.call_args.args[1]["maps"]), {"map_a"})
            self.assertEqual(promote.call_args.args[2], reports)
            record.assert_called_once()
            self.assertEqual(record.call_args.args[2], {"map_a": ("particles",)})

    def test_frozen_input_drift_rejects_before_verification_or_promotion(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            plan_path = Path(temporary) / "plan.json"
            plan_document = {
                "run_id": "test-run",
                "maps": {"test_map": {
                    "stages": ["textures"],
                    "fingerprints": {"textures": "frozen"},
                    "policies": {"textures": "policy"},
                }},
            }
            reports = {"test_map": {"stages": {
                "textures": {"built": 1, "reused": 0, "pruned": 0},
            }}}
            with (
                mock.patch.object(
                    export_manager.bake_cache, "plan_stages",
                    return_value={"test_map": ("textures",)}),
                mock.patch.object(
                    export_manager.bake_cache, "create_asset_run_plan",
                    return_value=(plan_path, plan_document)),
                mock.patch.object(
                    export_manager.bake_cache, "load_asset_run_reports",
                    return_value=reports),
                mock.patch.object(
                    export_manager.bake_cache, "assert_asset_run_inputs_current",
                    side_effect=RuntimeError("synthetic drift")),
                mock.patch.object(export_manager.bake_cache, "promote_asset_run") as promote,
                mock.patch.object(export_manager.bake_cache, "record_stages") as record,
                mock.patch.object(export_manager.unreal, "bake_maps"),
                mock.patch.object(export_manager.unreal, "verify_bakes") as verify,
            ):
                with self.assertRaises(export_manager.ExportBakeFailure):
                    export_manager.bake_and_verify(config, object(), ["test_map"])
            verify.assert_not_called()
            promote.assert_not_called()
            record.assert_not_called()

    def test_failed_verification_discards_only_the_verified_maps_receipts(self) -> None:
        # A receipt records what the bake authored, not whether it is right, so a map the
        # verification commandlet rejected has to bake again rather than answer from its receipt.
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            verified = export_manager.bake_cache.AssetReceiptStore(
                config.export_root, "test_map")
            verified.replace_stages({"level": {"policy": "policy", "assets": {}}})
            untouched = export_manager.bake_cache.AssetReceiptStore(
                config.export_root, "other_map")
            untouched.replace_stages({"level": {"policy": "policy", "assets": {}}})

            with (
                mock.patch.object(
                    export_manager.bake_cache, "plan_stages", return_value={}),
                mock.patch.object(
                    export_manager.unreal, "verify_bakes",
                    side_effect=RuntimeError("synthetic verification failure")) as verify,
            ):
                with self.assertRaises(export_manager.ExportBakeFailure):
                    export_manager.bake_and_verify(config, object(), ["test_map"])

            verify.assert_called_once_with(config, mock.ANY, ["test_map"])
            self.assertFalse(verified.path.exists())
            self.assertTrue(untouched.path.exists())

    def test_a_package_root_module_change_moves_the_decoder_fingerprint(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            package = config.repo_root / "pipeline" / "src" / "elysium_pipeline"
            (package / "formats").mkdir(parents=True)
            (package / "exporters").mkdir()
            (package / "formats" / "bsp.py").write_text("VALUE = 1\n", encoding="utf-8")
            (package / "exporters" / "UE_bsp_to_scene.py").write_text(
                "VALUE = 1\n", encoding="utf-8")
            for name in ("shared_corpus.py", "placed_models.py", "asset_names.py"):
                (package / name).write_text("VALUE = 1\n", encoding="utf-8")

            initial = export_manager._decoder_source_fingerprint(config)
            (package / "shared_corpus.py").write_text("VALUE = 222\n", encoding="utf-8")
            self.assertNotEqual(export_manager._decoder_source_fingerprint(config), initial)

    def test_policy_fingerprint_ignores_unrelated_bake_scripts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            unreal_root = config.repo_root / "pipeline" / "unreal"
            unreal_root.mkdir(parents=True)
            (unreal_root / "build_content.py").write_text(
                'GENERATORS = ["make_world_materials.py"]\n', encoding="utf-8"
            )
            generator = unreal_root / "make_world_materials.py"
            generator.write_text("VALUE = 1\n", encoding="utf-8")
            (unreal_root / "make_ui_fonts.py").write_text("VALUE = 1\n", encoding="utf-8")
            unrelated = unreal_root / "make_particle_systems.py"
            unrelated.write_text("VALUE = 1\n", encoding="utf-8")

            initial = export_manager._policy_fingerprint(config)
            unrelated.write_text("VALUE = 2\n", encoding="utf-8")
            self.assertEqual(export_manager._policy_fingerprint(config), initial)
            generator.write_text("VALUE = 2\n", encoding="utf-8")
            self.assertNotEqual(export_manager._policy_fingerprint(config), initial)

    def test_world_material_policy_has_its_own_fingerprint(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            unreal_root = config.repo_root / "pipeline" / "unreal"
            unreal_root.mkdir(parents=True)
            (unreal_root / "build_content.py").write_text(
                'GENERATORS = ["make_world_materials.py", "make_sky_material.py"]\n',
                encoding="utf-8")
            world = unreal_root / "make_world_materials.py"
            sky = unreal_root / "make_sky_material.py"
            world.write_text("VALUE = 1\n", encoding="utf-8")
            sky.write_text("VALUE = 1\n", encoding="utf-8")

            initial = export_manager._world_material_fingerprint(config)
            sky.write_text("VALUE = 2\n", encoding="utf-8")
            self.assertEqual(export_manager._world_material_fingerprint(config), initial)
            world.write_text("VALUE = 2\n", encoding="utf-8")
            self.assertNotEqual(export_manager._world_material_fingerprint(config), initial)

    def test_focused_world_policy_runs_only_the_world_material_generator(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            material_root = config.repo_root / "Content" / "VtMB" / "Materials"

            def generate(_config, _runner, generators, *, include_auxiliary):
                self.assertEqual(generators, (export_manager.WORLD_MATERIAL_GENERATOR,))
                self.assertFalse(include_auxiliary)
                material_root.mkdir(parents=True)
                for name in (
                    "M_World_Opaque", "M_World_Masked", "M_World_Translucent",
                    "M_World_Glass", "M_Refract", "M_Additive",
                ):
                    (material_root / f"{name}.uasset").write_bytes(b"asset")

            with mock.patch.object(
                export_manager.unreal, "generate_policy_content", side_effect=generate
            ) as policy:
                result = export_manager.ensure_world_material_content(config, object())

            self.assertEqual(result.status, "ok")
            policy.assert_called_once()

    def test_focused_character_policy_runs_only_character_material_generators(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            material_root = config.repo_root / "Content" / "VtMB" / "Materials"

            def generate(_config, _runner, generators, *, include_auxiliary):
                self.assertEqual(generators, export_manager.CHARACTER_MATERIAL_GENERATORS)
                self.assertFalse(include_auxiliary)
                material_root.mkdir(parents=True)
                (material_root / "M_PlayerBody.uasset").write_bytes(b"body")
                (material_root / "M_Eyes.uasset").write_bytes(b"eyes")

            with mock.patch.object(
                export_manager.unreal, "generate_policy_content", side_effect=generate
            ) as policy:
                result = export_manager.ensure_character_material_content(config, object())

            self.assertEqual(result.status, "ok")
            policy.assert_called_once()

    def test_focused_character_sources_name_only_the_body_and_reached_banks(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            npc_dir = config.export_root / "npc"
            npc_dir.mkdir()
            (npc_dir / "npc_manifest.json").write_text(json.dumps({
                "npcs": {
                    "amy": {
                        "model": "models/amy.mdl",
                        "clips": {"idle": "amy", "walk": "bank_a"},
                    },
                    "bob": {
                        "model": "models/bob.mdl",
                        "clips": {"idle": "bob", "walk": "bank_b"},
                    },
                },
                "banks": {
                    "bank_a": {"model": "models/bank_a.mdl"},
                    "bank_b": {"model": "models/bank_b.mdl"},
                },
                "cinematics": {},
                "placed_models": {
                    "switch": {"model": "models/switch.mdl", "clips": {}},
                },
            }), encoding="utf-8")
            reached = []

            def capture(graph, **_kwargs):
                reached.extend(graph.tasks)
                return {
                    name: TaskResult(name, "skipped", 0.0)
                    for name in graph.tasks
                }

            from elysium_pipeline.formats import install
            with (
                mock.patch.object(install, "build_index", return_value={}),
                mock.patch.object(export_manager.TaskGraph, "run", new=capture),
            ):
                bodies, banks = export_manager.write_character_sources(
                    config,
                    npc_dir,
                    manifest=Manifest(config.export_root / ".elysium-manifest.json"),
                    include_props=False,
                    body_stems=("amy",),
                )

            self.assertEqual(bodies, ["amy"])
            self.assertEqual(banks, ["bank_a"])
            self.assertEqual(reached, ["eskm:amy", "eskm:bank:bank_a"])

    def test_focused_placed_model_never_downgrades_global_clip_policy(self) -> None:
        rest = PlacedModelUse(
            "models/switch.mdl", "switch", "models_switch", False, ())
        full_row = {
            "model": "models/switch.mdl", "static_stem": "models_switch",
            "eskm": "placed_models/switch.eskm", "clip_mode": "full",
            "rest_candidates": ["idle"],
            "clips": {"idle": {}, "activate": {}, "deactivate": {}},
        }
        self.assertTrue(export_manager._placed_row_satisfies(full_row, rest))
        preserved = export_manager._preserve_placed_row_policy(rest, full_row)
        self.assertTrue(preserved.full_clips)

        required = PlacedModelUse(
            "models/switch.mdl", "switch", "models_switch", False,
            ("idle", "activate", "deactivate"))
        self.assertFalse(export_manager._placed_row_satisfies(full_row | {
            "clip_mode": "required",
            "clips": {"idle": {}, "activate": {}},
        }, required))
        self.assertTrue(export_manager._placed_row_satisfies(full_row, required))


if __name__ == "__main__":
    unittest.main()
