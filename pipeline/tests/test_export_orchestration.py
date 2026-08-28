from __future__ import annotations

import unittest
from unittest import mock

from elysium_pipeline import export_manager, wield_corpus
from elysium_pipeline.exporters import export_all


class ExportProfileTests(unittest.TestCase):
    def test_texture_glb_corpus_admits_each_tth_identity_once(self) -> None:
        index = {
            "materials/a/brick.tth": object(),
            "materials/a/brick.ttz": object(),
            "materials/b/glass.tth": object(),
            "materials/b/glass.vmt": object(),
            "models/not-a-texture.tth": object(),
        }
        self.assertEqual(
            export_manager._texture_glb_sources(index),
            ["a/brick", "b/glass"],
        )

    def test_all_texture_glbs_reuses_one_patch_first_index(self) -> None:
        from pathlib import Path
        from types import SimpleNamespace

        from elysium_pipeline.exporters import texture_glb
        from elysium_pipeline.formats import install
        from elysium_pipeline.validation import texture_glb as validation

        index = {
            "materials/a/brick.tth": object(),
            "materials/a/brick.ttz": object(),
            "materials/b/glass.tth": object(),
        }
        config = SimpleNamespace(
            game_root=Path("C:/game"),
            work_root=Path("C:/work"),
            export_root=Path("C:/export"),
        )

        def write(_index, texture, output_root):
            self.assertIs(_index, index)
            return output_root / (texture.replace("/", "_") + ".glb")

        summary = {
            "asset": "vtmb:texture:test",
            "accountedBytes": 10,
            "sourceBytes": 10,
        }
        with (
            mock.patch.object(install, "build_index", return_value=index) as build_index,
            mock.patch.object(texture_glb, "export", side_effect=write) as export,
            mock.patch.object(validation, "validate", return_value=summary) as validate,
        ):
            destinations = export_manager.export_all_texture_glbs(
                config, object(), jobs=1
            )

        self.assertEqual(len(destinations), 2)
        build_index.assert_called_once_with()
        self.assertEqual(export.call_count, 2)
        self.assertEqual(validate.call_count, 2)

    def test_character_glb_corpus_admits_only_models_with_topology(self) -> None:
        index = {
            "models/character/a/body.mdl": object(),
            "models/character/a/body.dx80.vtx": object(),
            "models/character/b/body.mdl": object(),
            "models/character/b/body.dx7_2bone.vtx": object(),
            "models/character/shared/bank.mdl": object(),
            "models/scenery/prop.mdl": object(),
            "models/scenery/prop.dx80.vtx": object(),
        }
        self.assertEqual(
            export_manager._character_glb_models(index),
            [
                "models/character/a/body.mdl",
                "models/character/b/body.mdl",
            ],
        )

    def test_all_character_glbs_reuses_one_index_and_anorm_table(self) -> None:
        from pathlib import Path
        from types import SimpleNamespace

        from elysium_pipeline.exporters import character_glb
        from elysium_pipeline.formats import install, mdl_skel
        from elysium_pipeline.validation import character_glb as validation

        index = {
            "models/character/a/body.mdl": object(),
            "models/character/a/body.dx80.vtx": object(),
            "models/character/b/body.mdl": object(),
            "models/character/b/body.dx7_2bone.vtx": object(),
        }
        config = SimpleNamespace(
            game_root=Path("C:/game"),
            work_root=Path("C:/work"),
            export_root=Path("C:/export"),
        )

        def write(_index, model, output_root, *, anorms):
            self.assertIs(_index, index)
            self.assertIs(anorms, vectors)
            return output_root / (model.rsplit("/", 1)[-1][:-4] + ".glb")

        vectors = [(1.0, 0.0, 0.0)]
        summary = {
            "asset": "vtmb:character-body:test",
            "accountedBytes": 10,
            "sourceBytes": 10,
        }
        with (
            mock.patch.object(install, "build_index", return_value=index) as build_index,
            mock.patch.object(mdl_skel, "load_anorms", return_value=vectors) as load_anorms,
            mock.patch.object(character_glb, "export", side_effect=write) as export,
            mock.patch.object(validation, "validate", return_value=summary) as validate,
        ):
            destinations = export_manager.export_all_character_glbs(
                config, object(), jobs=1
            )

        self.assertEqual(len(destinations), 2)
        build_index.assert_called_once_with()
        load_anorms.assert_called_once_with()
        self.assertEqual(export.call_count, 2)
        self.assertEqual(validate.call_count, 2)

    def test_glb_corpus_workers_are_spawn_importable(self) -> None:
        import pickle

        from elysium_pipeline import workers

        for worker in (workers.character_glb_worker, workers.texture_glb_worker):
            restored = pickle.loads(pickle.dumps(worker))
            self.assertEqual(restored.__name__, worker.__name__)

    def test_grid_is_the_canonical_test_set(self) -> None:
        maps = export_all.maps_for_profile("grid")
        self.assertEqual(maps[:2], ["sp_tutorial_1", "sp_theatre"])
        self.assertIn("sm_hub_1", maps)
        self.assertIn("la_hub_1", maps)
        self.assertEqual(len(maps), len(set(maps)))

    def test_profiles_include_every_required_offline_bundle(self) -> None:
        required = {
            "audio",
            "particles",
            "scripts",
            "signs",
            "vdata",
            "items",
            "cfg",
            "scenes",
            "ui",
            "use-icons",
            "npc",
        }
        for profile in ("grid", "all"):
            with self.subTest(profile=profile):
                self.assertEqual(set(export_all.bundles_for_profile(profile)), required)

    def test_only_ents_consuming_bundles_wait_on_the_maps(self) -> None:
        # `audio` and `npc` read the exported per-map `.ents`; every other bundle reads the
        # install (or the pre-graph shared corpus) and starts immediately. `npc` keeps its one
        # inter-bundle edge on the exported vdata mirror.
        from pathlib import Path
        from types import SimpleNamespace

        bundles = [
            "audio", "particles", "scripts", "signs", "vdata", "items",
            "cfg", "scenes", "ui", "use-icons", "npc",
        ]
        config = SimpleNamespace(export_root=Path("/fake/export/root"))
        tasks = export_manager._bundle_tasks(
            config, bundles, ["m1", "m2"], {}, {bundle: "fp" for bundle in bundles})
        by_name = {task.name: task for task in tasks}
        map_edges = ("map:m1", "map:m2")
        self.assertEqual(by_name["bundle:audio"].dependencies, map_edges)
        self.assertEqual(by_name["bundle:npc"].dependencies,
                         (*map_edges, "bundle:vdata"))
        for bundle in bundles:
            if bundle in ("audio", "npc"):
                continue
            with self.subTest(bundle=bundle):
                self.assertEqual(by_name[f"bundle:{bundle}"].dependencies, ())

    def test_npc_bundle_drops_the_vdata_edge_when_vdata_is_not_requested(self) -> None:
        from pathlib import Path
        from types import SimpleNamespace

        config = SimpleNamespace(export_root=Path("/fake/export/root"))
        tasks = export_manager._bundle_tasks(
            config, ["npc"], ["m1"], {}, {"npc": "fp"})
        self.assertEqual(tasks[0].dependencies, ("map:m1",))

    def test_items_bundle_outputs_include_both_manifests(self) -> None:
        from pathlib import Path

        export_root = Path("/fake/export/root")
        outputs = export_manager._bundle_outputs(export_root, "items")
        self.assertIn(export_root / "items" / "ground_models.json", outputs)
        self.assertIn(wield_corpus.manifest_path(export_root), outputs)

    def test_run_bundle_items_invokes_both_exporters(self) -> None:
        from elysium_pipeline.exporters import UE_extract_items, UE_extract_wield

        with (
            mock.patch.object(UE_extract_items, "main") as items_main,
            mock.patch.object(UE_extract_wield, "main") as wield_main,
        ):
            export_all._run_bundle("items", maps=(), force=True, inventory=True, index=None)

        items_main.assert_called_once_with(index=None, force=True)
        wield_main.assert_called_once_with(index=None, force=True)

    def test_structured_failure_is_strict(self) -> None:
        result = export_all.ExportBatchResult(
            maps=[
                export_all.ExportTaskResult(
                    name="missing_map",
                    status="failed",
                    error="not installed",
                )
            ]
        )
        with self.assertRaises(export_all.ExportFailed) as caught:
            result.require_success()
        self.assertIs(caught.exception.result, result)


if __name__ == "__main__":
    unittest.main()
