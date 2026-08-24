from __future__ import annotations

import unittest
from unittest import mock

from elysium_pipeline import export_manager, wield_corpus
from elysium_pipeline.exporters import export_all


class ExportProfileTests(unittest.TestCase):
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
