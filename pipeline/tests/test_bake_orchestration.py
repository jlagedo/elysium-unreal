from __future__ import annotations

import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from elysium_pipeline import export_manager, shared_corpus
from elysium_pipeline.placed_models import PlacedModelUse
from elysium_pipeline.tasking import Manifest, TaskFailure, TaskResult


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

    def test_bake_launches_once_per_batch_and_trusts_the_exit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            with (
                mock.patch.object(export_manager.unreal, "bake_maps") as bake,
                mock.patch.object(export_manager.unreal, "verify_bakes") as verify,
            ):
                export_manager.bake_and_verify(config, object(), ["test_map", "test_map"])
            bake.assert_called_once_with(config, mock.ANY, ["test_map"], force=False)
            verify.assert_not_called()

    def test_missing_baked_package_is_a_loud_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            with (
                mock.patch.object(export_manager.unreal, "bake_maps"),
                mock.patch.object(export_manager.unreal, "verify_bakes") as verify,
            ):
                with self.assertRaises(export_manager.ExportBakeFailure) as caught:
                    export_manager.bake_and_verify(config, object(), ["absent_map"])
            self.assertIn("absent_map", str(caught.exception))
            verify.assert_not_called()

    def test_verify_is_an_explicit_opt_in(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            with (
                mock.patch.object(export_manager.unreal, "bake_maps"),
                mock.patch.object(export_manager.unreal, "verify_bakes") as verify,
            ):
                export_manager.bake_and_verify(
                    config, object(), ["test_map"], verify=True
                )
            verify.assert_called_once_with(config, mock.ANY, ["test_map"])

    def test_force_reaches_the_commandlet(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            with (
                mock.patch.object(export_manager.unreal, "bake_maps") as bake,
                mock.patch.object(export_manager.unreal, "verify_bakes"),
            ):
                export_manager.bake_and_verify(
                    config, object(), ["test_map"], force=True
                )
            bake.assert_called_once_with(config, mock.ANY, ["test_map"], force=True)

    def test_corpus_bake_always_launches_and_forwards_force(self) -> None:
        # No fingerprint gates the launch: per-asset reuse is the commandlet's own decision,
        # read off each asset's recipe stamp.
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            with mock.patch.object(export_manager.unreal, "bake_corpus") as bake:
                export_manager.ensure_corpus_bake(config, object(), force=True)
                export_manager.ensure_corpus_bake(config, object())
            self.assertEqual(
                [call.kwargs["force"] for call in bake.call_args_list], [True, False])

    def test_scoped_fingerprint_moves_with_reached_modules_only(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            package = config.repo_root / "pipeline" / "src" / "elysium_pipeline"
            (package / "exporters").mkdir(parents=True)
            (package / "exporters" / "reader.py").write_text(
                "from elysium_pipeline import shared_corpus\nVALUE = 1\n",
                encoding="utf-8")
            (package / "exporters" / "other.py").write_text("VALUE = 1\n", encoding="utf-8")
            (package / "shared_corpus.py").write_text("VALUE = 1\n", encoding="utf-8")
            entries = {"elysium_pipeline.exporters.reader"}

            initial = export_manager._scoped_source_fingerprint(config, entries)
            # A module the closure reaches moves the fingerprint...
            (package / "shared_corpus.py").write_text("VALUE = 2\n", encoding="utf-8")
            export_manager._DECODER_CLOSURES.clear()
            moved = export_manager._scoped_source_fingerprint(config, entries)
            self.assertNotEqual(moved, initial)
            # ...and a module outside it does not.
            (package / "exporters" / "other.py").write_text("VALUE = 2\n", encoding="utf-8")
            export_manager._DECODER_CLOSURES.clear()
            self.assertEqual(
                export_manager._scoped_source_fingerprint(config, entries), moved)

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

    def test_policy_fingerprint_includes_input_prompt_sources(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            unreal_root = config.repo_root / "pipeline" / "unreal"
            unreal_root.mkdir(parents=True)
            (unreal_root / "build_content.py").write_text(
                'GENERATORS = ["make_input_glyphs.py"]\n', encoding="utf-8"
            )
            (unreal_root / "make_input_glyphs.py").write_text(
                "VALUE = 1\n", encoding="utf-8"
            )
            source = config.repo_root / "Content" / "InputPrompts" / "Kenney" / "glyph.png"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"first")

            initial = export_manager._policy_fingerprint(config)
            source.write_bytes(b"replacement")
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
