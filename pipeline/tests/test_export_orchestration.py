from __future__ import annotations

import unittest

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
            "scripts",
            "signs",
            "vdata",
            "cfg",
            "scenes",
            "ui",
            "use-icons",
            "npc",
        }
        for profile in ("grid", "all"):
            with self.subTest(profile=profile):
                self.assertEqual(set(export_all.bundles_for_profile(profile)), required)

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
