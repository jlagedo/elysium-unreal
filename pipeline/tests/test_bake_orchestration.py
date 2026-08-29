from __future__ import annotations

import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from elysium_pipeline import export_manager, shared_corpus, wield_corpus
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
            bake.assert_called_once_with(config, mock.ANY, ["test_map"], force=False,
                                         particles=False,
                                         batch_size=export_manager.unreal.MAP_BAKE_BATCH)
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

    def test_particle_pass_is_an_explicit_opt_in(self) -> None:
        """The map bake authors no Niagara system unless the caller asks for one.

        The pass force-deletes packages the asset compiler may still own, which crashes the
        editor, so it stays off the default path and rides `--particles` when wanted.
        """
        recorded: list[list[str]] = []

        class _Runner:
            def run(self, argv, **_kwargs):
                recorded.append([str(item) for item in argv])
                return SimpleNamespace(returncode=0)

        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            config.ue_root = Path(temporary) / "ue"
            config.project = config.repo_root / "ElysiumUE.uproject"
            editor = config.ue_root / "Engine" / "Binaries" / "Win64"
            editor.mkdir(parents=True)
            (editor / "UnrealEditor-Cmd.exe").write_bytes(b"")

            export_manager.unreal.bake_maps(config, _Runner(), ["test_map"])
            self.assertNotIn("-BakeParticles=1", recorded[-1])

            export_manager.unreal.bake_maps(config, _Runner(), ["test_map"], particles=True)
            self.assertIn("-BakeParticles=1", recorded[-1])

    def test_particle_pass_is_part_of_the_profile_recipe(self) -> None:
        """Turning the pass on or off changes the maps-bake receipt, so a toggled run relaunches."""
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            (config.export_root / "test_map").mkdir(parents=True, exist_ok=True)
            off = export_manager._maps_bake_fingerprint(config, ["test_map"], particles=False)
            on = export_manager._maps_bake_fingerprint(config, ["test_map"], particles=True)
            self.assertNotEqual(off, on)

    def test_corpus_bake_gate_skips_a_warm_second_run_and_force_defeats_it(self) -> None:
        # The launch rides a manifest receipt over the decoded shared corpus; per-asset reuse
        # inside a launch remains the commandlet's own decision, read off each recipe stamp.
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            shared = config.export_root / "shared"
            shared.mkdir()
            (shared / "manifest.json").write_text("{}", encoding="utf-8")
            mount = config.repo_root / "Plugins" / "ElysiumBaked" / "Content" / "Shared"
            mount.mkdir(parents=True)
            with mock.patch.object(export_manager.unreal, "bake_corpus") as bake:
                export_manager.ensure_corpus_bake(config, object())
                export_manager.ensure_corpus_bake(config, object())
                export_manager.ensure_corpus_bake(config, object(), force=True)
            self.assertEqual(
                [call.kwargs["force"] for call in bake.call_args_list], [False, True])

    def test_corpus_bake_relaunches_when_a_shared_input_moves(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            shared = config.export_root / "shared"
            shared.mkdir()
            (shared / "manifest.json").write_text("{}", encoding="utf-8")
            mount = config.repo_root / "Plugins" / "ElysiumBaked" / "Content" / "Shared"
            mount.mkdir(parents=True)
            with mock.patch.object(export_manager.unreal, "bake_corpus") as bake:
                export_manager.ensure_corpus_bake(config, object())
                (shared / "manifest.json").write_text('{"textures": {}}', encoding="utf-8")
                export_manager.ensure_corpus_bake(config, object())
            self.assertEqual(bake.call_count, 2)

    def test_failed_corpus_bake_records_no_success(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            shared = config.export_root / "shared"
            shared.mkdir()
            (shared / "manifest.json").write_text("{}", encoding="utf-8")
            mount = config.repo_root / "Plugins" / "ElysiumBaked" / "Content" / "Shared"
            mount.mkdir(parents=True)
            with mock.patch.object(
                export_manager.unreal, "bake_corpus",
                side_effect=export_manager.unreal.UnrealFailure("editor exited with 1"),
            ):
                with self.assertRaises(export_manager.ExportBakeFailure):
                    export_manager.ensure_corpus_bake(config, object())
            # The failed launch left no usable receipt, so the next run launches again.
            with mock.patch.object(export_manager.unreal, "bake_corpus") as bake:
                export_manager.ensure_corpus_bake(config, object())
            bake.assert_called_once()

    def test_wield_bake_gate_skips_a_warm_second_run_and_force_defeats_it(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            manifest_path = wield_corpus.manifest_path(config.export_root)
            manifest_path.parent.mkdir(parents=True, exist_ok=True)
            manifest_path.write_text("{}", encoding="utf-8")
            wield_corpus.wield_dir(config.export_root).mkdir(parents=True, exist_ok=True)
            asset = (config.repo_root / "Plugins" / "ElysiumBaked" / "Content" / "Items"
                     / "DA_WieldModels.uasset")
            asset.parent.mkdir(parents=True)
            asset.write_bytes(b"table")
            with mock.patch.object(export_manager.unreal, "bake_wield") as bake:
                export_manager._ensure_wield_bake(config, object())
                export_manager._ensure_wield_bake(config, object())
                export_manager._ensure_wield_bake(config, object(), force=True)
            self.assertEqual(bake.call_count, 2)

    def test_profile_map_bake_gate_skips_warm_and_verify_still_reads_back(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            map_dir = config.export_root / "test_map"
            map_dir.mkdir()
            (map_dir / "test_map.obj").write_text("o test\n", encoding="utf-8")
            with (
                mock.patch.object(export_manager.unreal, "bake_maps") as bake,
                mock.patch.object(export_manager.unreal, "verify_bakes") as verify,
            ):
                export_manager._bake_profile_maps(config, object(), ["test_map"])
                export_manager._bake_profile_maps(config, object(), ["test_map"])
                export_manager._bake_profile_maps(config, object(), ["test_map"],
                                                  verify=True)
                export_manager._bake_profile_maps(config, object(), ["test_map"],
                                                  force=True)
            self.assertEqual(bake.call_count, 2)
            # --verify is an explicit read-back request and runs even off a warm receipt.
            self.assertEqual(verify.call_count, 1)

    def test_profile_map_bake_skip_requires_the_baked_package(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            map_dir = config.export_root / "test_map"
            map_dir.mkdir()
            (map_dir / "test_map.obj").write_text("o test\n", encoding="utf-8")
            package = export_manager._baked_package(config, "test_map")

            with mock.patch.object(export_manager.unreal, "bake_maps") as bake:
                export_manager._bake_profile_maps(config, object(), ["test_map"])
                package.unlink()
                bake.side_effect = lambda *_args, **_kwargs: package.write_bytes(b"level")
                export_manager._bake_profile_maps(config, object(), ["test_map"])
            self.assertEqual(bake.call_count, 2)

    def test_character_bake_gate_skips_warm_and_missing_mesh_defeats_it(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            npc_dir = config.export_root / "npc"
            npc_dir.mkdir()
            (npc_dir / "families.json").write_text("{}", encoding="utf-8")
            mesh = (config.repo_root / "Plugins" / "ElysiumBaked" / "Content"
                    / "Characters" / "Meshes" / "SK_amy.uasset")
            mesh.parent.mkdir(parents=True)

            def author(*_args, **_kwargs):
                mesh.write_bytes(b"mesh")

            with (
                mock.patch.object(export_manager.unreal, "bake_characters",
                                  side_effect=author) as bake,
                mock.patch.object(export_manager.unreal, "verify_characters") as verify,
            ):
                export_manager._run_character_bake(config, object(), ["amy"])
                export_manager._run_character_bake(config, object(), ["amy"])
                mesh.unlink()
                export_manager._run_character_bake(config, object(), ["amy"])
                export_manager._run_character_bake(config, object(), ["amy"], force=True)
            self.assertEqual(bake.call_count, 3)
            verify.assert_not_called()

    def test_policy_generators_merge_into_one_commandlet_launch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            unreal_root = config.repo_root / "pipeline" / "unreal"
            unreal_root.mkdir(parents=True)
            names = [
                export_manager.WORLD_MATERIAL_GENERATOR,
                *export_manager.CHARACTER_MATERIAL_GENERATORS,
                "make_boot_map.py",
            ]
            (unreal_root / "build_content.py").write_text(
                "GENERATORS = " + repr(names) + "\n", encoding="utf-8")
            for name in names:
                (unreal_root / name).write_text("VALUE = 1\n", encoding="utf-8")
            material_root = config.repo_root / "Content" / "ElysiumGenerated" / "Materials"
            font_root = config.repo_root / "Content" / "ElysiumGenerated" / "UI" / "Fonts"

            def generate(_config, _runner, generators, *, include_auxiliary):
                self.assertFalse(include_auxiliary)
                material_root.mkdir(parents=True, exist_ok=True)
                for master in (
                    "M_World_Opaque", "M_World_Masked", "M_World_Translucent",
                    "M_World_Glass", "M_Refract", "M_Additive", "M_PlayerBody", "M_Eyes",
                ):
                    (material_root / f"{master}.uasset").write_bytes(b"asset")

            def auxiliary(_config, _runner):
                font_root.mkdir(parents=True, exist_ok=True)
                for name in export_manager.unreal.FONT_ASSETS:
                    (font_root / name).write_bytes(b"font")
                graph = config.repo_root / "Content" / "ElysiumGenerated" / "Animation"
                graph.mkdir(parents=True, exist_ok=True)
                (graph / "ABP_ElysiumBiped.uasset").write_bytes(b"graph")
                (config.repo_root / "Content" / "ElysiumGenerated" / "Boot.umap").write_bytes(
                    b"map")

            with (
                mock.patch.object(export_manager.unreal, "generate_policy_content",
                                  side_effect=generate) as content,
                mock.patch.object(export_manager.unreal,
                                  "generate_auxiliary_policy_content",
                                  side_effect=auxiliary) as extras,
            ):
                first = export_manager.ensure_policy_content(
                    config, object(), particles_covered=True)
                second = export_manager.ensure_policy_content(
                    config, object(), particles_covered=True)
                third = export_manager.ensure_policy_content(
                    config, object(), particles_covered=True, force=True)

            # All three receipt sets were stale together, so one commandlet carried the
            # union in the generator list's declared order; the auxiliary pair launched
            # once behind it.
            self.assertEqual(content.call_count, 2)
            self.assertEqual(content.call_args_list[0].args[2], names)
            self.assertEqual(extras.call_count, 2)
            self.assertEqual(first.status, "ok")
            self.assertEqual(second.status, "skipped")
            self.assertEqual(third.status, "ok")

    def test_stale_world_materials_alone_launch_only_that_generator(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            unreal_root = config.repo_root / "pipeline" / "unreal"
            unreal_root.mkdir(parents=True)
            names = [
                export_manager.WORLD_MATERIAL_GENERATOR,
                *export_manager.CHARACTER_MATERIAL_GENERATORS,
                "make_boot_map.py",
            ]
            (unreal_root / "build_content.py").write_text(
                "GENERATORS = " + repr(names) + "\n", encoding="utf-8")
            for name in names:
                (unreal_root / name).write_text("VALUE = 1\n", encoding="utf-8")
            material_root = config.repo_root / "Content" / "ElysiumGenerated" / "Materials"
            font_root = config.repo_root / "Content" / "ElysiumGenerated" / "UI" / "Fonts"

            def generate(_config, _runner, generators, *, include_auxiliary):
                material_root.mkdir(parents=True, exist_ok=True)
                for master in (
                    "M_World_Opaque", "M_World_Masked", "M_World_Translucent",
                    "M_World_Glass", "M_Refract", "M_Additive", "M_PlayerBody", "M_Eyes",
                ):
                    (material_root / f"{master}.uasset").write_bytes(b"asset")

            def auxiliary(_config, _runner):
                font_root.mkdir(parents=True, exist_ok=True)
                for name in export_manager.unreal.FONT_ASSETS:
                    (font_root / name).write_bytes(b"font")
                graph = config.repo_root / "Content" / "ElysiumGenerated" / "Animation"
                graph.mkdir(parents=True, exist_ok=True)
                (graph / "ABP_ElysiumBiped.uasset").write_bytes(b"graph")
                (config.repo_root / "Content" / "ElysiumGenerated" / "Boot.umap").write_bytes(
                    b"map")

            with (
                mock.patch.object(export_manager.unreal, "generate_policy_content",
                                  side_effect=generate) as content,
                mock.patch.object(export_manager.unreal,
                                  "generate_auxiliary_policy_content",
                                  side_effect=auxiliary) as extras,
            ):
                export_manager.ensure_policy_content(
                    config, object(), particles_covered=True)
                # Invalidate only the world-material set; the merged launch then carries
                # exactly that generator and the auxiliary pair stays down.
                (unreal_root / export_manager.WORLD_MATERIAL_GENERATOR).write_text(
                    "VALUE = 2  # moved\n", encoding="utf-8")
                export_manager.ensure_policy_content(
                    config, object(), particles_covered=True)

            self.assertEqual(content.call_count, 2)
            self.assertEqual(
                content.call_args_list[1].args[2],
                [export_manager.WORLD_MATERIAL_GENERATOR])
            self.assertEqual(extras.call_count, 1)

    def test_export_profile_sequences_gated_launches_and_covers_particles(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            order = []
            with (
                mock.patch.object(
                    export_manager, "run_offline_profile",
                    side_effect=lambda *_a, **_k: order.append("offline") or (["m1"], {})),
                mock.patch.object(
                    export_manager, "ensure_policy_content",
                    side_effect=lambda *_a, **k: order.append(
                        ("policy", k.get("particles_covered")))),
                mock.patch.object(
                    export_manager, "ensure_corpus_bake",
                    side_effect=lambda *_a, **_k: order.append("corpus")),
                mock.patch.object(
                    export_manager, "export_characters",
                    side_effect=lambda *_a, **_k: order.append("characters")),
                mock.patch.object(
                    export_manager, "_ensure_wield_bake",
                    side_effect=lambda *_a, **_k: order.append("wield")),
                mock.patch.object(
                    export_manager, "_bake_profile_maps",
                    side_effect=lambda *_a, **_k: order.append("maps")),
            ):
                maps = export_manager.export_profile(config, object(), "all")
            self.assertEqual(maps, ["m1"])
            # The `all` profile carries the particles bundle, so the policy phase skips its
            # duplicate mirror; the cast and wield mounts precede the map bake.
            self.assertEqual(order, [
                "offline", ("policy", True), "corpus", "characters", "wield", "maps"])

    def test_particle_mirror_is_gated_and_skipped_when_a_profile_covers_it(self) -> None:
        from elysium_pipeline.exporters import UE_extract_particles
        from elysium_pipeline.formats import install

        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            manifest = Manifest(config.export_root / ".elysium-manifest.json")
            out = config.export_root / "particles"

            def mirror(**_kwargs):
                out.mkdir(parents=True, exist_ok=True)
                (out / "manifest.json").write_text("{}", encoding="utf-8")

            with (
                mock.patch.object(UE_extract_particles, "main", side_effect=mirror) as main,
                mock.patch.object(install, "build_index", return_value={}),
            ):
                export_manager._ensure_particle_mirror(
                    config, manifest=manifest, covered=True)
                main.assert_not_called()
                export_manager._ensure_particle_mirror(
                    config, manifest=manifest, covered=False)
                export_manager._ensure_particle_mirror(
                    config, manifest=manifest, covered=False)
                self.assertEqual(main.call_count, 1)
                export_manager._ensure_particle_mirror(
                    config, manifest=manifest, covered=False, force=True)
                self.assertEqual(main.call_count, 2)

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

    def test_focused_world_policy_runs_only_the_world_material_generator(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            material_root = config.repo_root / "Content" / "ElysiumGenerated" / "Materials"

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
            material_root = config.repo_root / "Content" / "ElysiumGenerated" / "Materials"

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
